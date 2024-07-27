/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/SaintsRow.h"

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

const uint32_t kTitleIdSaintsRow2 = 0x545107FC;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  uint32_t x_address;
  std::string title_version;
  uint32_t y_address;
};

std::map<SaintsRowGame::GameBuild, GameBuildAddrs> supported_builds{
    {SaintsRowGame::GameBuild::Unknown, {NULL, "", NULL}},
    {SaintsRowGame::GameBuild::SaintsRow2_TU3,
     {0x82B7A570, "8.0.3", 0x82B7A590}}};

SaintsRowGame::~SaintsRowGame() = default;

bool SaintsRowGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdSaintsRow2) {
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

float SaintsRowGame::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float SaintsRowGame::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}

bool SaintsRowGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                            X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  if (supported_builds.count(game_build_) == 0) {
    return false;
  }

  // Don't constantly write if there is no mouse movement.
  if (input_state.mouse.x_delta == 0 || input_state.mouse.y_delta == 0) {
    return false;
  }

  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }

  xe::be<float>* radian_x = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].x_address);

  xe::be<float>* radian_y = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].y_address);

  if (!radian_x || *radian_x == NULL) {
    // Not in game
    return false;
  }

  float degree_x = RadianstoDegree(*radian_x);
  float degree_y = RadianstoDegree(*radian_y);

  // X-axis = 0 to 360
  if (!cvars::invert_x) {
    degree_x += (input_state.mouse.x_delta / 5.f) * (float)cvars::sensitivity;
  } else {
    degree_x -= (input_state.mouse.x_delta / 5.f) * (float)cvars::sensitivity;
  }

  *radian_x = DegreetoRadians(degree_x);

  if (!cvars::invert_y) {
    degree_y += (input_state.mouse.y_delta / 5.f) * (float)cvars::sensitivity;
  } else {
    degree_y -= (input_state.mouse.y_delta / 5.f) * (float)cvars::sensitivity;
  }

  *radian_y = DegreetoRadians(degree_y);

  return true;
}

std::string SaintsRowGame::ChooseBinds() { return "Default"; }

bool SaintsRowGame::ModifierKeyHandler(uint32_t user_index,
                                       RawInputState& input_state,
                                       X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe