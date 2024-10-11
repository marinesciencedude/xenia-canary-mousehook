/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/JustCause.h"

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
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);

const uint32_t kTitleIdJustCause = 0x534307D5;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t cameracontroller_pointer_address;
  uint32_t SteerAddYaw_offset;
  uint32_t SteerAddPitch_offset;
};

std::map<JustCauseGame::GameBuild, GameBuildAddrs> supported_builds{
    {JustCauseGame::GameBuild::JustCause1_TU0,
     {"1.0", 0x46965100, 0x130, 0x12C}}};

JustCauseGame::~JustCauseGame() = default;

bool JustCauseGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdJustCause) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    if (current_version == build.second.title_version) {
      game_build_ = build.first;
      return true;
    }
  }

  return false;
}

/* float JustCauseGame::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float JustCauseGame::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}
*/

bool JustCauseGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                            X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  if (supported_builds.count(game_build_) == 0) {
    return false;
  }

  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }

  /*
TODO: Vehicle Camera which is CMachineCamera
and Possibly turrets which is CMountedGunCamera

Some addresses used are in radians,
Vehicle in-game camera is more closer to racing games cameras than a traditional
GTA-styled freecam.

*/

  xe::be<uint32_t>* base_address =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
          supported_builds[game_build_].cameracontroller_pointer_address);

  if (!base_address || *base_address == NULL) {
    // Not in game
    return false;
  }

  xe::be<uint32_t> x_address =
      *base_address + supported_builds[game_build_].SteerAddYaw_offset;
  xe::be<uint32_t> y_address =
      *base_address + supported_builds[game_build_].SteerAddPitch_offset;

  xe::be<float>* add_x =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(x_address);

  xe::be<float>* add_y =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(y_address);

  float camx = *add_x;
  float camy = *add_y;
  // X-axis = 0 to 360
  if (!cvars::invert_x) {
    camx += (input_state.mouse.x_delta / 5.f) * (float)cvars::sensitivity;

  } else {
    camx -= (input_state.mouse.x_delta / 5.f) * (float)cvars::sensitivity;
  }
  *add_x = camx;
  // Y-axis = -90 to 90
  if (!cvars::invert_y) {
    camy += (input_state.mouse.y_delta / 5.f) * (float)cvars::sensitivity;
  } else {
    camy -= (input_state.mouse.y_delta / 5.f) * (float)cvars::sensitivity;
  }
  *add_y = camy;

  return true;
}

std::string JustCauseGame::ChooseBinds() { return "Default"; }

bool JustCauseGame::ModifierKeyHandler(uint32_t user_index,
                                       RawInputState& input_state,
                                       X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe