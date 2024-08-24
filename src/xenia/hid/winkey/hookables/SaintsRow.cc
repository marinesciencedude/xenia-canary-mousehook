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
DECLARE_bool(disable_autoaim);
DECLARE_double(right_stick_hold_time_workaround);

const uint32_t kTitleIdSaintsRow2 = 0x545107FC;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t x_address;
  uint32_t y_address;
  uint32_t player_status_address;
  uint32_t pressB_status_address;
  uint32_t menu_status_address;
  uint32_t sniper_status_address;
};

std::map<SaintsRowGame::GameBuild, GameBuildAddrs> supported_builds{
    {SaintsRowGame::GameBuild::Unknown, {"", NULL, NULL, NULL, NULL, NULL}},
    {SaintsRowGame::GameBuild::SaintsRow2_TU3,
     {"8.0.3", 0x82B7A570, 0x82B7A590, 0x82B7ABC4, 0x837B79C3, 0x82B58DA0,
      0x82BCBA78}}};

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

  auto* sniper_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].sniper_status_address);

  float divisor = (*sniper_status == 0) ? 50.f : 10.f;

  // X-axis = 0 to 360
  if (!cvars::invert_x) {
    degree_x +=
        (input_state.mouse.x_delta / divisor) * (float)cvars::sensitivity;
  } else {
    degree_x -=
        (input_state.mouse.x_delta / divisor) * (float)cvars::sensitivity;
  }

  *radian_x = DegreetoRadians(degree_x);

  if (!cvars::invert_y) {
    degree_y +=
        (input_state.mouse.y_delta / divisor) * (float)cvars::sensitivity;
  } else {
    degree_y -=
        (input_state.mouse.y_delta / divisor) * (float)cvars::sensitivity;
  }

  *radian_y = DegreetoRadians(degree_y);

  return true;
}

std::string SaintsRowGame::ChooseBinds() {
  // Highest priority:
  auto* wheel_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].pressB_status_address);

  auto* menu_status = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
      supported_builds[game_build_].menu_status_address);

  if (wheel_status && *wheel_status != 0 &&
      *menu_status == 2) {  // Need to check menu_status otherwise pressing B in
                            // some menus will cause you to get stuck if
                            // WheelOpen binds difer from Menu.
    return "WheelOpen";
  }

  // Check the menu status next

  if (menu_status && *menu_status != 2) {
    return "Menu";
  }

  // Check the player status next
  auto* player_status = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
      supported_builds[game_build_].player_status_address);
  if (player_status) {
    switch (*player_status) {
      case 3:
      case 5:
        return "Vehicle";
      case 6:
        return "Helicopter";
      case 8:
        return "Aircraft";
      default:
        break;
    }
  }

  return "Default";
}

bool SaintsRowGame::ModifierKeyHandler(uint32_t user_index,
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
  // Return true to signal that we've handled the modifier, so default modifier
  // won't be used
  return true;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe