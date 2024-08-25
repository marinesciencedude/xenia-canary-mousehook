/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/DeadRising.h"

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

const uint32_t kTitleIdDR2CZ = 0x58410A8D;
const uint32_t kTitleIdDR2CW = 0x58410B00;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  uint32_t title_id;
  uint32_t x_address;
  uint32_t y_address;
};

std::map<DeadRisingGame::GameBuild, GameBuildAddrs> supported_builds{
    {DeadRisingGame::GameBuild::Unknown, {NULL, NULL, NULL}},
    {DeadRisingGame::GameBuild::DeadRising2_CaseZero,
     {kTitleIdDR2CZ, 0xAA4D2388, 0xAA4D238C}},
    {DeadRisingGame::GameBuild::DeadRising2_CaseWest,
     {kTitleIdDR2CW, 0xA94DF458, 0xA94DF45C}}};

DeadRisingGame::~DeadRisingGame() = default;

bool DeadRisingGame::IsGameSupported() {
  auto title_id = kernel_state()->title_id();
  if (title_id != kTitleIdDR2CZ && title_id != kTitleIdDR2CW) {
    return false;
  }
  const std::string current_version =
      kernel_state()->emulator()->title_version();
  for (auto& build : supported_builds) {
    if (title_id == build.second.title_id) {
      game_build_ = build.first;
      return true;
    }
  }
  return true;
}

float DeadRisingGame::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float DeadRisingGame::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}

bool DeadRisingGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                             X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
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
    degree_x -= (input_state.mouse.x_delta / 5.f) * (float)cvars::sensitivity;
  } else {
    degree_x += (input_state.mouse.x_delta / 5.f) * (float)cvars::sensitivity;
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

std::string DeadRisingGame::ChooseBinds() { return "Default"; }

bool DeadRisingGame::ModifierKeyHandler(uint32_t user_index,
                                        RawInputState& input_state,
                                        X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe