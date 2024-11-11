/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/SaintsRow1.h"

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
DECLARE_bool(sr_havok_fix_frametime);
DECLARE_bool(swap_wheel);
DECLARE_double(menu_sensitivity);

const uint32_t kTitleIdSaintsRow1 = 0x545107D1;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t x_address;
  uint32_t y_address;
  uint32_t fineaim_y_address;
  uint32_t map_x_address;
  uint32_t map_zoom_address;
  uint32_t pause_screen_section_address;
  uint32_t vehicle_address;
  uint32_t weapon_wheel_address;
  uint32_t weapon_wheel_slot_address;
  uint32_t menu_status_address;
  uint32_t havok_frametime_address;
  uint32_t current_frametime_address;  //       x_axis_addition =
                                       //       -(float)((float)_FP12 /
                                       //       current_frametime);
  uint32_t ingame_sens;
  uint32_t current_fov_address;
  uint32_t isfirstperson_address;  // Unused game camera mode ; toggleable with
                                   // a console command mostly usable with
                                   // Tervel's sr1fineaim plugin.

  uint32_t slow_pan_horizontal_multiplier_address;
};

std::map<SaintsRow1Game::GameBuild, GameBuildAddrs> supported_builds{
    {SaintsRow1Game::GameBuild::Unknown, {" ", NULL, NULL}},
    {SaintsRow1Game::GameBuild::SaintsRow1_TU1,
     {"1.0.1", 0x827f9af8, 0x827F9B00, 0x827F9BA4, 0x835F2B80, 0x827CF9CC,
      0x835F279B, 0x82932407, 0x8283CA7B, 0x835F2883, 0x835F27A3, 0x835F2684,
      0x827CA69C, 0x827F9AD8, 0x827F9B58, 0x827F99C7, 0x827F956C}}};

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

  // REMOVE THIS FOR RELEASE NEEDS TO BE A PATCH!
  // xtbl edits can't be made into a patch most likely?
  xe::be<float>* ingamesens_x =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].ingame_sens);

  xe::be<float>* slow_pan_horizontal_multiplier =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].slow_pan_horizontal_multiplier_address);

  xe::be<float>* slow_pan_vertical_multiplier =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].slow_pan_horizontal_multiplier_address +
          0x4);

  xe::be<float>* ingamesens_y =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].ingame_sens + 0x4);

  if (*ingamesens_x != 0.01999999955f || *ingamesens_y != 0.01999999955f) {
    *ingamesens_x = 0.01999999955f;
    *ingamesens_y = 0.01999999955f;
  }

  if (*slow_pan_vertical_multiplier != 0.00009999999747f ||
      *slow_pan_horizontal_multiplier != 0.00009999999747f) {
    *slow_pan_horizontal_multiplier = 0.00009999999747f;
    *slow_pan_vertical_multiplier = 0.00009999999747f;
  }

  xe::be<float>* ingame_frametime =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].current_frametime_address);

  float frametime = *ingame_frametime;
  if (cvars::sr_havok_fix_frametime && !isTervelPlugin()) {
    FixHavokFrameTime(frametime);
  }

  // float correctFrametime = 1 / *currentFPS;

  //*frametime = correctFrametime * 2;

  auto now = std::chrono::steady_clock::now();
  auto elapsed_x = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_movement_time_x_)
                       .count();
  auto elapsed_y = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_movement_time_y_)
                       .count();

  if (!(inFirstPerson() && isTervelPlugin()) && !inMapScreen()) {
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
  }

  if ((!input_state.mouse.x_delta && !input_state.mouse.y_delta &&
       !input_state.mouse.wheel_delta)) {
    return false;
  }

  if (inMapScreen()) {
    MapCursor(input_state);
  }

  if (isPaused()) {
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

  static xe::be<float>* fine_aim_x = NULL;
  static xe::be<float>* fine_aim_y = NULL;
  if (inFirstPerson() && isTervelPlugin()) {
    divider_x = 15.f;
    frametime = 1.f;

    fine_aim_x = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        supported_builds[game_build_].fineaim_y_address + 0x4);

    fine_aim_y = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        supported_builds[game_build_].fineaim_y_address);
    degree_x = RadianstoDegree(*fine_aim_x);
  }

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
  // frame? -= isn't ideal but that's the only way it works. - Clippy95
  if (!cvars::invert_x) {
    degree_x +=
        ((input_state.mouse.x_delta / divider_x) * (float)cvars::sensitivity) /
        frametime;
  } else {
    degree_x -=
        ((input_state.mouse.x_delta / divider_x) * (float)cvars::sensitivity) /
        frametime;
  }
  if (!(inFirstPerson() && isTervelPlugin())) {
    *addition_x = degree_x;
  } else if (*fine_aim_x != NULL) {
    *fine_aim_x = DegreetoRadians(degree_x);
  }

  float delta_y =
      (input_state.mouse.y_delta / divider_y) * (float)cvars::sensitivity;

  if (cvars::invert_y) {
    delta_y = -delta_y;
  }

  degree_y += delta_y;
  *radian_y = DegreetoRadians(degree_y);
  if ((inFirstPerson() && isTervelPlugin())) {
    degree_y = RadianstoDegree(*fine_aim_y);
    degree_y += delta_y;
    *fine_aim_y = DegreetoRadians(degree_y);
  }
  if (*wheel_status == 1) {
    WeaponWheelScrollWheel(input_state);
  }
  return true;
}

void SaintsRow1Game::FixHavokFrameTime(float frametime) {
  XThread* current_thread = XThread::GetCurrentThread();
  if (!current_thread) {
    return;
  }
  xe::be<float>* havok_frametime =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].havok_frametime_address);

  if (frametime < 0.03333333333f) {
    frametime = frametime / 2.f;
    if (*havok_frametime != frametime) {
      *havok_frametime = frametime;
    }
  } else {
    if (*havok_frametime != 0.01666666666f) {
      *havok_frametime = 0.01666666666f;
    }
  }
}

bool SaintsRow1Game::isTervelPlugin() {
  /* Although the fineaim option exists as a console command, realistically
     users will be using a plugin to switch to it. DoHooks only checks
     isTervelPlugin when supported game is loaded, we can't hog GetModule
     otherwise it causes an impact performance according to SourceEngine.cc
     */
  if (!isPaused()) {
    if (tervelplugin_status == 0) {
      if (kernel_state()->GetModule("sr1fineaim.xex")) {
        tervelplugin_status = 1;
        return true;
      } else {
        tervelplugin_status = 2;
        return false;
      }
      return false;
    }
    if (tervelplugin_status == 1) {
      return true;
    } else {
      return false;
    }

    return false;
  } else {
    return false;
  }
}

bool SaintsRow1Game::inFirstPerson() {
  auto* firstperson = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].isfirstperson_address);
  if (*firstperson && *firstperson == 1) {
    return true;
  } else {
    return false;
  }
}

bool SaintsRow1Game::isPaused() {
  auto* pause_flag = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].menu_status_address);

  if (*pause_flag != 2) {
    return true;
  } else {
    return false;
  }
}

void SaintsRow1Game::WeaponWheelScrollWheel(RawInputState& input_state) {
  auto* weapon_slot = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].weapon_wheel_slot_address);
  if (input_state.mouse.wheel_delta) {
    int16_t slot = static_cast<int16_t>(*weapon_slot);

    // one scroll of the wheel_delta seems to always return 120?
    if (!cvars::swap_wheel) {
      slot += static_cast<int16_t>(input_state.mouse.wheel_delta / 120);
    } else {
      slot -= static_cast<int16_t>(input_state.mouse.wheel_delta / 120);
    }

    slot = slot % 8;

    *weapon_slot = static_cast<uint8_t>(slot);
  }
}

bool SaintsRow1Game::inMapScreen() {
  auto* pause_screen = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].pause_screen_section_address);

  if (*pause_screen == 26 && isPaused()) {
    return true;
  } else {
    return false;
  }
}

void SaintsRow1Game::MapCursor(RawInputState& input_state) {
  xe::be<float>* map_x_be = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].map_x_address);

  xe::be<float>* map_y_be = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].map_x_address + 0x4);

  xe::be<float>* map_zoom_be =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].map_zoom_address);

  float map_x = *map_x_be;

  float map_y = *map_y_be;

  float map_zoom = *map_zoom_be;

  // 3.75 * 0.2 = 0.75 when zoomed out the farthest game allows.
  map_x -= (input_state.mouse.x_delta / (3.75f * map_zoom)) *
           (float)cvars::menu_sensitivity;

  map_y -= (input_state.mouse.y_delta / (3.75f * map_zoom)) *
           (float)cvars::menu_sensitivity;

  if (!cvars::swap_wheel) {
    map_zoom += (input_state.mouse.wheel_delta / (1000.f / map_zoom));
  } else {
    map_zoom -= (input_state.mouse.wheel_delta / (1000.f / map_zoom));
  }
  map_x = std::clamp(map_x, -1677.760498f, 1677.760498f);
  map_y = std::clamp(map_y, -2245.578369f, 2245.578369f);

  // game default clamping is between 0.2 and 1, the game does allow to write
  // outside of those, so I set the minimum a bit lower as that feels more
  // natural with a mouse?
  map_zoom = std::clamp(map_zoom, 0.1f, 2.5f);

  *map_x_be = map_x;
  *map_y_be = map_y;
  *map_zoom_be = map_zoom;
}

std::string SaintsRow1Game::ChooseBinds() {
  wheel_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].weapon_wheel_address);
  auto* menu_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].menu_status_address);
  auto* vehicle_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].vehicle_address);

  if (*wheel_status == 1 || (menu_status && *menu_status != 2)) {
    return "Default";
  }
  if (vehicle_status && *vehicle_status == 1) {
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
