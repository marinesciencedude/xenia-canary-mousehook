/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/GearsOfWars.h"

#include "xenia/base/chrono.h"
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

const uint32_t kTitleIdGearsOfWars3 = 0x4D5308AB;
const uint32_t kTitleIdGearsOfWars2 = 0x4D53082D;
const uint32_t kTitleIdGearsOfWars1 = 0x4D5307D5;
const uint32_t kTitleIdGearsOfWarsJudgment = 0x4D530A26;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t camera_base_address;
  uint32_t x_offset;
  uint32_t y_offset;
  uint32_t menu_status_address;
};

std::map<GearsOfWarsGame::GameBuild, GameBuildAddrs> supported_builds{
    {GearsOfWarsGame::GameBuild::Unknown, {"", NULL, NULL, NULL}},
    {GearsOfWarsGame::GameBuild::GearsOfWars2_TU6,
     {"5.0.6", 0x40874800, 0x66, 0x62, 0x83146F3F}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU0,
     {"11.0", 0x43F6F340, 0x66, 0x62, 0x83146F3F}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU6,
     {"9.0.6", 0x42145D40, 0x66, 0x62, 0x83146F3F}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU0_XBL,
     {"9.0", 0x43F6F340, 0x66, 0x62, 0x83146F3F}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU6_XBL,
     {"9.0.6", 0x42145D40, 0x66, 0x62, 0x83146F3F}},
    {GearsOfWarsGame::GameBuild::GearsOfWarsJudgment_TU0,
     {"9.0", 0x448F2840, 0x66, 0x62, 0x83146F3F}},
    {GearsOfWarsGame::GameBuild::GearsOfWarsJudgment_TU4,
     {"9.0.4", 0x42943440, 0x66, 0x62, 0x83146F3F}},
    {GearsOfWarsGame::GameBuild::GearsOfWars1_TU0,
     {"1.0", 0x49EAC460, 0xDE, 0xDA, 0x83146F3F}}};

GearsOfWarsGame::~GearsOfWarsGame() = default;

bool GearsOfWarsGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdGearsOfWars3 &&
      kernel_state()->title_id() != kTitleIdGearsOfWars2 &&
      kernel_state()->title_id() != kTitleIdGearsOfWars1 &&
      kernel_state()->title_id() != kTitleIdGearsOfWarsJudgment) {
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

bool GearsOfWarsGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                              X_INPUT_STATE* out_state) {
  static bool bypass_conditions =
      false;  // This will be set to true after 2 minutes
  static auto start_time = std::chrono::steady_clock::now();
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
  auto* menu_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].menu_status_address);
  if (!bypass_conditions) {
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(
        current_time - start_time);

    if (elapsed_time.count() >= 15) {
      bypass_conditions = true;
    }
  }

  // If the conditions are bypassed (after 2 minutes), run the code
  if (bypass_conditions) {
    xe::be<uint16_t>* degree_x;
    xe::be<uint16_t>* degree_y;

    uint32_t base_address =
        *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].camera_base_address);
    if (base_address &&
        base_address <
            0x0000000050000000) {  // timer isn't enough, check location it's
                                   // most likely between 40000000 - 50000000,
                                   // thanks Marine.
      degree_x = kernel_memory()->TranslateVirtual<xe::be<uint16_t>*>(
          base_address + supported_builds[game_build_].x_offset);

      degree_y = kernel_memory()->TranslateVirtual<xe::be<uint16_t>*>(
          base_address + supported_builds[game_build_].y_offset);

      uint16_t x_delta = static_cast<uint16_t>(
          (input_state.mouse.x_delta * 10) * cvars::sensitivity);
      uint16_t y_delta = static_cast<uint16_t>(
          (input_state.mouse.y_delta * 10) * cvars::sensitivity);
      if (!cvars::invert_x) {
        *degree_x += x_delta;
      } else {
        *degree_x -= x_delta;
      }

      if (!cvars::invert_y) {
        *degree_y -= y_delta;
      } else {
        *degree_y += y_delta;
      }
    }
  }
  return true;
}

std::string GearsOfWarsGame::ChooseBinds() { return "Default"; }

bool GearsOfWarsGame::ModifierKeyHandler(uint32_t user_index,
                                         RawInputState& input_state,
                                         X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe