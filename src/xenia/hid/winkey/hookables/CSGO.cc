/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/hookables/CSGO.h"

#include "xenia/base/platform_win.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
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

const uint32_t kTitleIdCSGO = 0x5841125A;
const std::string kBetaVersion = "1.0.1.16";

namespace xe {
namespace hid {
namespace winkey {

bool __inline IsKeyToggled(uint8_t key) {
  return (GetKeyState(key) & 0x1) == 0x1;
}

CSGOGame::CSGOGame() {
  original_sensitivity = cvars::sensitivity;
  engine_360 = NULL;
  isBeta = false;
};

CSGOGame::~CSGOGame() = default;

bool CSGOGame::IsGameSupported() {
  return kernel_state()->title_id() == kTitleIdCSGO;
}

#define IS_KEY_DOWN(x) (input_state.key_states[x])

bool CSGOGame::DoHooks(uint32_t user_index, RawInputState& input_state,
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

  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }

  if (kernel_state()->emulator()->title_version() == kBetaVersion) {
    isBeta = true;
  }

  current_thread->thread_state()->context()->r[3] = -1;

  if (isBeta) {
    kernel_state()->processor()->Execute(current_thread->thread_state(),
                                         0x8697DB30);
  } else {
    kernel_state()->processor()->Execute(current_thread->thread_state(),
                                         0x86955490);
  }

  // Get player pointer
  uint32_t player_ptr =
      static_cast<uint32_t>(current_thread->thread_state()->context()->r[3]);

  if (!player_ptr) {
    // Not in game
    return false;
  }

  xe::be<uint32_t>* angle_offset;

  if (isBeta) {
    angle_offset = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
        player_ptr + 0x4AC8);
  } else {
    angle_offset = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
        player_ptr + 0xB60);
  }

  QAngle* ang = reinterpret_cast<QAngle*>(angle_offset);

  // std::string angState = fmt::format("Angle: y: {} x: {} z: {}", ang->pitchY,
  //                                    ang->pitchX, ang->yaw);

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

  return true;
}

bool CSGOGame::ModifierKeyHandler(uint32_t user_index,
                                  RawInputState& input_state,
                                  X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe