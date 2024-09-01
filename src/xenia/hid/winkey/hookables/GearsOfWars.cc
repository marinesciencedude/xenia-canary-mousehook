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
DECLARE_double(right_stick_hold_time_workaround);
DECLARE_bool(use_right_stick_workaround);
DECLARE_bool(use_right_stick_workaround_gears1and2);

const uint32_t kTitleIdGearsOfWars3 = 0x4D5308AB;
const uint32_t kTitleIdGearsOfWars2 = 0x4D53082D;
const uint32_t kTitleIdGearsOfWars1 = 0x4D5307D5;
const uint32_t kTitleIdGearsOfWarsJudgment = 0x4D530A26;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  uint32_t title_id;
  const char* title_version;
  uint32_t camera_base_address;
  uint32_t x_offset;
  uint32_t y_offset;
  uint32_t LookRightScale_address;
};

std::map<GearsOfWarsGame::GameBuild, GameBuildAddrs> supported_builds{
    {GearsOfWarsGame::GameBuild::Unknown, {NULL, "", NULL, NULL, NULL}},
    {GearsOfWarsGame::GameBuild::GearsOfWars2_TU6,
     {kTitleIdGearsOfWars2, "5.0.6", 0x40874800, 0x66, 0x62, 0x404E8840}},
    {GearsOfWarsGame::GameBuild::GearsOfWars2_TU6_XBL,
     {kTitleIdGearsOfWars2, "6.0.6", 0x40874800, 0x66, 0x62, 0x404E8840}},
    {GearsOfWarsGame::GameBuild::GearsOfWars2_TU0_XBL,
     {kTitleIdGearsOfWars2, "6.0", 0x408211C0, 0x66, 0x62, 0x405294C0}},
    {GearsOfWarsGame::GameBuild::GearsOfWars2_TU0,
     {kTitleIdGearsOfWars2, "5.0", 0x408211C0, 0x66, 0x62, 0x405294C0}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU0,
     {kTitleIdGearsOfWars3, "11.0", 0x43F6F340, 0x66, 0x62, 0x404E18F0}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU6,
     {kTitleIdGearsOfWars3, "9.0.6", 0x42145D40, 0x66, 0x62, 0x40502254}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU0_XBL,
     {kTitleIdGearsOfWars3, "9.0", 0x43F6F340, 0x66, 0x62, 0x404E18F0}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU6_XBL,
     {kTitleIdGearsOfWars3, "11.0.6", 0x42145D40, 0x66, 0x62, 0x40502254}},
    {GearsOfWarsGame::GameBuild::GearsOfWarsJudgment_TU0,
     {kTitleIdGearsOfWarsJudgment, "9.0", 0x448F2840, 0x66, 0x62, 0x41DE7054}},
    {GearsOfWarsGame::GameBuild::GearsOfWarsJudgment_TU4,
     {kTitleIdGearsOfWarsJudgment, "9.0.4", 0x42943440, 0x66, 0x62,
      0x41F2F754}},
    {GearsOfWarsGame::GameBuild::GearsOfWars1_TU0,
     {kTitleIdGearsOfWars1, "1.0", 0x49EAC460, 0xDE, 0xDA, 0x40BF0164}},
    {GearsOfWarsGame::GameBuild::GearsOfWars1_TU5,
     {kTitleIdGearsOfWars1, "1.0.5", 0x4A1CBA60, 0xDE, 0xDA, 0x40BF9814}}};

GearsOfWarsGame::~GearsOfWarsGame() = default;

bool GearsOfWarsGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdGearsOfWars3 &&
      kernel_state()->title_id() != kTitleIdGearsOfWars2 &&
      kernel_state()->title_id() != kTitleIdGearsOfWars1 &&
      kernel_state()->title_id() != kTitleIdGearsOfWarsJudgment) {
    return false;
  }
  uint32_t title_id = kernel_state()->title_id();
  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    // Match the version and title ID to ensure the correct build
    if (current_version == build.second.title_version &&
        title_id == build.second.title_id) {
      game_build_ = build.first;

      // Check if 15 seconds have passed before proceeding
      static bool bypass_conditions = false;
      static auto start_time = std::chrono::steady_clock::now();

      if (!bypass_conditions) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(
            current_time - start_time);

        if (elapsed_time.count() >= 15) {
          bypass_conditions = true;
        }
      }

      if (bypass_conditions &&
          supported_builds[game_build_].LookRightScale_address &&
          ((cvars::use_right_stick_workaround_gears1and2 &&
            (title_id == kTitleIdGearsOfWars1 ||
             title_id == kTitleIdGearsOfWars2)) ||
           (cvars::use_right_stick_workaround &&
            (title_id == kTitleIdGearsOfWars3 ||
             title_id == kTitleIdGearsOfWarsJudgment)))) {
        xe::be<float>* LookRightScale =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(
                supported_builds[game_build_].LookRightScale_address);
        xe::be<float>* LookUpScale =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(
                supported_builds[game_build_].LookRightScale_address + 0x4);

        // Check if LookRightScale equals 0.1 (big-endian)
        if (*LookRightScale != 0.05f) {
          // If it does not equal 0.1, set LookRightScale and LookUpScale to 0.1
          *LookRightScale = 0.05f;
          *LookUpScale = 0.05f;
        }
      }

      return true;
    }
  }

  return false;
}

bool GearsOfWarsGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                              X_INPUT_STATE* out_state) {
  static bool bypass_conditions =
      false;  // This will be set to true after some time.
  static auto start_time = std::chrono::steady_clock::now();
  if (!IsGameSupported()) {
    return false;
  }

  if (supported_builds.count(game_build_) == 0) {
    return false;
  }
  uint32_t title_id = kernel_state()->title_id();
  if (supported_builds[game_build_].LookRightScale_address &&
      ((cvars::use_right_stick_workaround_gears1and2 &&
        (title_id == kTitleIdGearsOfWars1 ||
         title_id == kTitleIdGearsOfWars2)) ||
       (cvars::use_right_stick_workaround &&
        (title_id == kTitleIdGearsOfWars3 ||
         title_id == kTitleIdGearsOfWarsJudgment)))) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_x = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - last_movement_time_x_)
                         .count();
    auto elapsed_y = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - last_movement_time_y_)
                         .count();

    // Declare static variables for last deltas
    static int last_x_delta = 0;
    static int last_y_delta = 0;

    const long long hold_time =
        static_cast<long long>(cvars::right_stick_hold_time_workaround);
    // Check for mouse movement and set thumbstick values
    if (input_state.mouse.x_delta != 0) {
      if (input_state.mouse.x_delta > 0) {
        out_state->gamepad.thumb_rx = SHRT_MAX;
      } else {
        out_state->gamepad.thumb_rx = SHRT_MIN;
      }
      last_movement_time_x_ = now;
      last_x_delta = input_state.mouse.x_delta;
    } else if (elapsed_x < hold_time) {  // hold time
      if (last_x_delta > 0) {
        out_state->gamepad.thumb_rx = SHRT_MAX;
      } else {
        out_state->gamepad.thumb_rx = SHRT_MIN;
      }
    }

    if (input_state.mouse.y_delta != 0) {
      if (input_state.mouse.y_delta > 0) {
        out_state->gamepad.thumb_ry = SHRT_MAX;
      } else {
        out_state->gamepad.thumb_ry = SHRT_MIN;
      }
      last_movement_time_y_ = now;
      last_y_delta = input_state.mouse.y_delta;
    } else if (elapsed_y < hold_time) {  // hold time
      if (last_y_delta > 0) {
        out_state->gamepad.thumb_ry = SHRT_MIN;
      } else {
        out_state->gamepad.thumb_ry = SHRT_MAX;
      }
    }

    // Return true if either X or Y delta is non-zero or if within the hold time
    if (input_state.mouse.x_delta == 0 && input_state.mouse.y_delta == 0 &&
        elapsed_x >= hold_time && elapsed_y >= hold_time) {
      return false;
    }
  }

  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }
  if (!bypass_conditions) {
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(
        current_time - start_time);

    if (elapsed_time.count() >= 15) {
      bypass_conditions = true;
    }
  }

  if (bypass_conditions) {
    xe::be<uint16_t>* degree_x;
    xe::be<uint16_t>* degree_y;
    // printf("Current Build: %d\n", static_cast<int>(game_build_));
    uint32_t base_address =
        *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].camera_base_address);
    // printf("BASE ADDRESS: 0x%08X\n", base_address);
    if (base_address &&
        base_address <
            0x0000000050000000) {  // timer isn't enough, check location it's
                                   // most likely between 40000000 - 50000000,
                                   // thanks Marine.
      degree_x = kernel_memory()->TranslateVirtual<xe::be<uint16_t>*>(
          base_address + supported_builds[game_build_].x_offset);
      // printf("DEGREE_X ADDRESS: 0x%08X\n",
      //     (base_address + supported_builds[game_build_].x_offset));

      degree_y = kernel_memory()->TranslateVirtual<xe::be<uint16_t>*>(
          base_address + supported_builds[game_build_].y_offset);
      // printf("DEGREE_Y ADDRESS: 0x%08X\n",
      //        (base_address + supported_builds[game_build_].x_offset));
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
  float thumb_lx = (int16_t)out_state->gamepad.thumb_lx;
  float thumb_ly = (int16_t)out_state->gamepad.thumb_ly;

  if (thumb_lx != 0 ||
      thumb_ly !=
          0) {  // Required otherwise stick is pushed to the right by default.
    // Work out angle from the current stick values
    float angle = atan2f(thumb_ly, thumb_lx);

    // Sticks get set to SHRT_MAX if key pressed, use half of that
    float distance = (float)SHRT_MAX;
    distance /= 2;

    out_state->gamepad.thumb_lx = (int16_t)(distance * cosf(angle));
    out_state->gamepad.thumb_ly = (int16_t)(distance * sinf(angle));
  }
  return true;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe