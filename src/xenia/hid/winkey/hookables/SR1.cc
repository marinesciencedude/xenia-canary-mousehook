/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/SR1.h"

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

const uint32_t kTitleIdSaintsRow1 = 0x545107D1;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t x_address;
  uint32_t y_address;
  uint32_t vehicle_address;
  uint32_t weapon_wheel_address;
  uint32_t menu_status_address;
  uint32_t currentFPS_address;
  uint32_t current_frametime_address;  //       x_axis_addition =
                                       //       -(float)((float)_FP12 /
                                       //       current_frametime);
  uint32_t ingame_sens;
  uint32_t current_fov_address;
};

std::map<SaintsRow1Game::GameBuild, GameBuildAddrs> supported_builds{
    {SaintsRow1Game::GameBuild::Unknown, {" ", NULL, NULL}},
    {SaintsRow1Game::GameBuild::SaintsRow1_TU1,
     {"1.0.1", 0x827f9af8, 0x827F9B00, 0x836117D7, 0x8283CA7B, 0x835F27A3,
      0x827CA750, 0x827CA69C, 0x827F9AD8, 0x827F9B58}}};

SaintsRow1Game::~SaintsRow1Game() = default;

bool SaintsRow1Game::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdSaintsRow1) {
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

float SaintsRow1Game::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float SaintsRow1Game::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}

bool SaintsRow1Game::DoHooks(uint32_t user_index, RawInputState& input_state,
                             X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  if (supported_builds.count(game_build_) == 0) {
    return false;
  }
  xe::be<float>* currentFPS = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].currentFPS_address);

  // REMOVE THIS FOR RELEASE NEEDS TO BE A PATCH!
  xe::be<float>* ingamesens_x =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].ingame_sens);
  float x_sens = *ingamesens_x;
  xe::be<float>* ingamesens_y =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].ingame_sens + 0x4);

  if (x_sens != 0.01999999955f) {
    *ingamesens_x = 0.01999999955f;
    *ingamesens_y = 0.01999999955f;
  }

  xe::be<float>* ingame_frametime =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].current_frametime_address);

  float frametime = *ingame_frametime;

  // float correctFrametime = 1 / *currentFPS;

  //*frametime = correctFrametime * 2;

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

  xe::be<float>* addition_x = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].x_address);

  xe::be<float>* radian_y = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].y_address);

  xe::be<float>* current_fov =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].current_fov_address);

  float degree_x = *addition_x;
  float degree_y = RadianstoDegree(*radian_y);
  float fov = *current_fov;

  float divider_y = 15.f;
  float divider_x = 1350.f;

  if (fov < 60.f) {
    fov = 60.f / fov;
    divider_y = divider_y * fov;
    divider_x = divider_x * fov;
  }

  // X-axis = 0 to 360
  // division over 1350 is assuming if frametime is 1/30, this should fix
  // sensitivity fluctuation due to framerate as that's what the game does at
  // 8249DD28(TU1); x_axis_addition = -(float)((float)_FP12 / frametime);
  // stuttering might still occur due to framerates, as it's expected each
  // frame? -= isn't ideal but that's the only way it works.
  if (!cvars::invert_x) {
    degree_x +=
        ((input_state.mouse.x_delta / divider_x) * (float)cvars::sensitivity) /
        frametime;
  } else {
    degree_x -=
        ((input_state.mouse.x_delta / divider_x) * (float)cvars::sensitivity) /
        frametime;
  }

  *addition_x = degree_x;

  if (!cvars::invert_y) {
    degree_y +=
        (input_state.mouse.y_delta / divider_y) * (float)cvars::sensitivity;
  } else {
    degree_y -=
        (input_state.mouse.y_delta / divider_y) * (float)cvars::sensitivity;
  }

  *radian_y = DegreetoRadians(degree_y);
  return true;
}
std::string SaintsRow1Game::ChooseBinds() {
  auto* wheel_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].weapon_wheel_address);
  auto* menu_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].menu_status_address);
  auto* vehicle_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].vehicle_address);

  if (*wheel_status == 1) {
    return "Default";
  }
  /* if (menu_status && *menu_status != 2) {
   return "Menu";
 }*/
  if (vehicle_status && *vehicle_status != 0) {
    return "Vehicle";
  }

  return "Default";
}
bool SaintsRow1Game::ModifierKeyHandler(uint32_t user_index,
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