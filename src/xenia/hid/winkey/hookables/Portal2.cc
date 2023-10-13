/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/hookables/Portal2.h"

#include "xenia/base/platform_win.h"
#include "xenia/cpu/processor.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

using namespace xe::kernel;

DECLARE_double(sensitivity);
DECLARE_double(source_sniper_sensitivity);
DECLARE_bool(invert_y);

const uint32_t kTitleIdPortal2 = 0x45410912;

namespace xe {
namespace hid {
namespace winkey {

bool __inline IsKeyToggled(uint8_t key) {
  return (GetKeyState(key) & 0x1) == 0x1;
}

Portal2Game::Portal2Game() { 
    original_sensitivity = cvars::sensitivity;
    engine_360 = NULL;
};

Portal2Game::~Portal2Game() = default;

bool Portal2Game::IsGameSupported() {
  return kernel_state()->title_id() == kTitleIdPortal2;
}

#define IS_KEY_DOWN(x) (input_state.key_states[x])

bool Portal2Game::DoHooks(uint32_t user_index, RawInputState& input_state,
                       X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  // Wait until module is loaded, if loaded don't check again otherwise it will
  // impact performance.
  if (!engine_360) {
    if (!kernel_state()->GetModule("engine_360.dll")) {
      return false;
    } else {
      engine_360 = true;
    }
  }

  xe::kernel::XThread* current_thread = xe::kernel::XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }

  current_thread->thread_state()->context()->r[3] = -1;

  bool result = kernel_state()->processor()->Execute(
      current_thread->thread_state(), 0x82C50180);

  // Get player pointer
  uint32_t player_ptr =
      static_cast<uint32_t>(current_thread->thread_state()->context()->r[3]);

  if (!player_ptr) {
    // Not in game
    return false;
  }

  xe::be<uint32_t>* angle_offset =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player_ptr + 0x4A98);

  if (!result) return false;

  QAngle* ang = reinterpret_cast<QAngle*>(angle_offset);

  //std::string angState = fmt::format("Angle: y: {} x: {} z: {}", ang->pitchY,
  //                                   ang->pitchX, ang->yaw);

  // Have to do weird things converting it to normal float otherwise
  // xe::be += treats things as int?
  float camX = (float)ang->pitchX;
  float camY = (float)ang->pitchY;

  if (cvars::source_sniper_sensitivity != 0) {
    if (IsKeyToggled(VK_CAPITAL) != 0) {
      cvars::sensitivity = cvars::source_sniper_sensitivity;
    } else {
      cvars::sensitivity = original_sensitivity;
    }
  }

  camX -=
      (((float)input_state.mouse.x_delta) / 1000.f) * (float)cvars::sensitivity;

  if (!cvars::invert_y) {
    camY -= (((float)input_state.mouse.y_delta) / 1000.f) *
            (float)cvars::sensitivity;
  } else {
    camY += (((float)input_state.mouse.y_delta) / 1000.f) *
            (float)cvars::sensitivity;
  }

  ang->pitchX = camX;
  ang->pitchY = camY;

  if (!result) return false;

  return true;
}

bool Portal2Game::ModifierKeyHandler(uint32_t user_index,
                                  RawInputState& input_state,
                                  X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe