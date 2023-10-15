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
DECLARE_int32(walk_orthogonal);
DECLARE_int32(walk_diagonal);

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
        player_ptr + 0x4AE8);
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

// probably making a mistake using a template, ah well
template <typename T> int sgn(T val)
{
  return (T(0) < val) - (val < T(0));
}

bool CSGOGame::ModifierKeyHandler(uint32_t user_index,
                                       RawInputState& input_state,
                                       X_INPUT_STATE* out_state)
{
  float thumb_lx = (int16_t)out_state->gamepad.thumb_lx;
  float thumb_ly = (int16_t)out_state->gamepad.thumb_ly;

  // Work out angle from the current stick values
  float angle = atan2f(thumb_ly, thumb_lx);

  // Equates to 134.99 h.u./s - 22800 for forward/backward, 18421 for diagonal
  int16_t distance_x, distance_y;

  // as soon as you put it to a separate variable it stops bugging out

  int multiplier_x = sgn(thumb_lx);
  int multiplier_y = sgn(thumb_ly);

  // If angle DIV π⁄4 is odd
  if (fmod(angle / 0.785398185f, 2) != 0)
  { 
    distance_x = int16_t(cvars::walk_diagonal * multiplier_x);
    distance_y = int16_t(cvars::walk_diagonal * multiplier_y);
  }
  else 
  {
      // Default value equates to 134.99 h.u./s, any higher than this value and the movement speed immediately goes to max
    distance_x = int16_t(cvars::walk_orthogonal * multiplier_x);  // Default value between SHRT_MAX * (177.4/255 and 177.5/255)
    distance_y = int16_t(cvars::walk_orthogonal * multiplier_y);  // Default value between SHRT_MAX * (177.4/255 and 177.5/255)
  }

  // Need analogue-compatible version
  //out_state->gamepad.thumb_lx = (int16_t)(distance * cosf(angle));
  //out_state->gamepad.thumb_ly = (int16_t)(distance * sinf(angle));
  out_state->gamepad.thumb_lx = distance_x;
  out_state->gamepad.thumb_ly = distance_y;

  // Return true to signal that we've handled the modifier, so default modifier won't be used
  return true;


}

}  // namespace winkey
}  // namespace hid
}  // namespace xe