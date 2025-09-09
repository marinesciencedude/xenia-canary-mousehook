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
DECLARE_bool(swap_wheel);
DECLARE_double(menu_sensitivity);
DECLARE_bool(internal_hook);

const uint32_t kTitleIdSaintsRow1Global = 0x545107D1;
const uint32_t kTitleIdSaintsRow1JP = 0x545107F8;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t x_address;
  uint32_t y_address;
  uint32_t fineaim_y_address;
  uint32_t player_address;
  uint32_t map_x_address;
  uint32_t map_zoom_address;
  uint32_t pause_screen_section_address;
  uint32_t current_diversion_type_addr;  // 0x82F062EF different candidate
  uint32_t vehicle_address;
  uint32_t weapon_wheel_address;
  uint32_t weapon_wheel_slot_address;
  uint32_t food_wheel_object_address;
  uint32_t food_wheel_slot_address;
  uint32_t menu_status_address;
  // ACTUAL pause flag, menu_status_address acts more like if can player control
  // (kind of the real player paused controls flag is at 0x8283CA7B, will need
  // to use this if problems arise.)
  uint32_t world_paused_address;
  uint32_t current_frametime_address;  //       x_axis_addition =
                                       //       -(float)((float)_FP12 /
                                       //       current_frametime);
  uint32_t ingame_sens;
  uint32_t current_fov_address;
  uint32_t isfirstperson_address;  // Unused game camera mode ; toggleable with
                                   // a console command mostly usable with
                                   // Tervel's sr1fineaim plugin.

  uint32_t slow_pan_horizontal_multiplier_address;
  uint32_t change_weapon_function_addr;
  uint32_t limit_weapons_function_addr;
  uint32_t allowable_weapons_melee_array;
  uint32_t can_spin_player_flag_addr;
  uint32_t rims_jobs_address_ptr;
  uint32_t customization_screen_zoom_level_addr;
  uint32_t mp_flag_1;
  uint32_t mp_flag_2;
  uint32_t radian_x_axis_midhook_addr1;
  uint32_t reload_players_weapon_addr;
};
SaintsRow1Game* SaintsRow1Game::current_instance_ = nullptr;
std::map<SaintsRow1Game::GameBuild, GameBuildAddrs> supported_builds{
    {SaintsRow1Game::GameBuild::Unknown, {"", NULL, NULL}},
    {SaintsRow1Game::GameBuild::SaintsRow1_TU1,
     {"0.0.1.1",  0x827f9af8, 0x827F9B00, 0x827F9BA4, 0x82F7EB04, 0x835F2B80,
      0x827CF9CC, 0x835F279B, 0x82EDE231, 0x82932407, 0x8283CA7B, 0x835F2883,
      0x82EE12F4, 0x835F2884, 0x835F27A3, 0x835F2527, 0x827CA69C, 0x827F9AD8,
      0x827F9B58, 0x827F99A3, 0x827F956C, 0x822AEB78, 0x822ADC10, 0x827D0484,
      0x835F1A58, 0x837DD080, 0x827F95B4, 0x835F33DF, 0x835F3522, 0x8211D3E8,
      0x82496428}},
    {SaintsRow1Game::GameBuild::SaintsRow1_JP,
     {"0.0.0.1",  0x827E9BF8, 0x827E9C00, 0x827E9CA4, 0x82F6E69C, 0x835E2718,
      0x827BFAD0, 0x835E232F, 0x82EE2566, 0x82922507, 0x8282CB7B, 0x835E241B,
      0x82ED13AC, 0x835E241C, 0x835E233B, 0x835E20C7, 0x827BA764, 0x827E9BD8,
      0x827E9C58, 0x827E9AA3, 0x827E966C, 0x822AD718, 0x822AC730, 0x827C058C,
      0x835E15F0, 0x837CCC10, 0x827E96B4, 0x835E2F6E, 0x835E2F6F, 0x8211D428,
      0x824930C8}}};
SaintsRow1Game::~SaintsRow1Game() = default;
std::map<std::pair<uint32_t, std::string>, SaintsRow1Game::GameBuild>
    supported_builds_lookup{{{kTitleIdSaintsRow1Global, "0.0.1.1"},
                             SaintsRow1Game::GameBuild::SaintsRow1_TU1},
                            {{kTitleIdSaintsRow1JP, "0.0.0.1"},
                             SaintsRow1Game::GameBuild::SaintsRow1_JP}};

std::map<std::string, GameVersion> supported_sr1_versions{
    {"0.0.1.1", {0, 0, 1, 1}},  // TU1 US version
    {"0.0.0.1",
     {0, 0, 0, 1}}  // JP version (and TU0 US, but we'll filter by title ID)
};

bool SaintsRow1Game::IsGameSupported(GameVersion title_version) {
  auto title_id = kernel_state()->title_id();
  if (title_id != kTitleIdSaintsRow1JP &&
      title_id != kTitleIdSaintsRow1Global) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  // Create lookup key with title ID and version
  auto lookup_key = std::make_pair(title_id, current_version);
  auto build_it = supported_builds_lookup.find(lookup_key);

  if (build_it != supported_builds_lookup.end()) {
    auto version_it = supported_sr1_versions.find(current_version);
    if (version_it != supported_sr1_versions.end()) {
      GameVersion build_version = version_it->second;
      if (std::memcmp(&title_version, &build_version, sizeof(GameVersion)) ==
          0) {
        game_build_ = build_it->second;
        return true;
      }
    }
  }

#ifdef XENIA_MOUSEHOOK_MESSAGE
  mousehook_message_wrapper(
      std::format("MOUSEHOOK: Supported Title ID, but current version '{}' is "
                  "unsupported. Expected: TU1 US (0.0.1.1) or JP (0.0.0.1)",
                  current_version),
      true, MESSAGE_TYPE_XNotify);
#endif
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
  if (supported_builds.count(game_build_) == 0) {
    return false;
  }
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
    *ingamesens_x = 0.01999999955f;  // 3CA3D70A
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

  auto now = std::chrono::steady_clock::now();
  auto elapsed_x = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_movement_time_x_)
                       .count();
  auto elapsed_y = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_movement_time_y_)
                       .count();

  if (!(isTervelPlugin() && inFirstPerson()) && !inMapScreen() &&
      !(canspinplayer && isPaused())) {
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
  player = *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
      supported_builds[game_build_].player_address);

  if (isPaused()) {
    if (inMapScreen()) {
      MapCursor(input_state);
    }
    if (*kernel_memory()->TranslateVirtual<uint8_t*>(
            supported_builds[game_build_].world_paused_address) != 0) {
      return false;
    }
    if (RotatePlayerinCustomization(input_state) == true) {
      return false;
    }

    return false;
  }
  if (input_state.mouse.wheel_delta) {
    WeaponWheelScrollWheel(input_state);
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
  float divider_x = 15.f;

  static xe::be<float>* fine_aim_x = NULL;
  static xe::be<float>* fine_aim_y = NULL;
  if (isTervelPlugin() && inFirstPerson()) {
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

  if (!cvars::invert_x) {
    degree_x +=
        ((input_state.mouse.x_delta / divider_x) * (float)cvars::sensitivity);
  } else {
    degree_x -=
        ((input_state.mouse.x_delta / divider_x) * (float)cvars::sensitivity);
  }
  if (!cvars::internal_hook && !(isTervelPlugin() && inFirstPerson())) {
    *addition_x = DegreetoRadians(degree_x);
  } else if (fine_aim_x != NULL && (isTervelPlugin() && inFirstPerson())) {
    *fine_aim_x = DegreetoRadians(degree_x);
  }
  mouse_x_ld += input_state.mouse.x_delta;
  float delta_y =
      (input_state.mouse.y_delta / divider_y) * (float)cvars::sensitivity;

  if (cvars::invert_y) {
    delta_y = -delta_y;
  }

  degree_y += delta_y;
  degree_y = std::clamp(degree_y, -90.f, 90.f);
  *radian_y = DegreetoRadians(degree_y);
  if ((isTervelPlugin() && inFirstPerson())) {
    degree_y = RadianstoDegree(*fine_aim_y);
    degree_y += delta_y;
    degree_y = std::clamp(degree_y, -90.f, 90.f);
    *fine_aim_y = DegreetoRadians(degree_y);
  }

  return true;
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
  if (*firstperson && *firstperson == 8) {
    return true;
  } else {
    return false;
  }
}

bool SaintsRow1Game::isMP() {
  auto* mp1 = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].mp_flag_1);
  auto* mp2 = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].mp_flag_2);
  if (*mp2 == 1 || *mp1 == 1) {
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

bool SaintsRow1Game::RotatePlayerinCustomization(RawInputState& input_state) {
  if (player == NULL) {
    return false;
  }
  canspinplayer = *kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].can_spin_player_flag_addr);
  if (canspinplayer != 1) {
    return false;
  }

  float mousex =
      (input_state.mouse.x_delta / 5.f) * (float)cvars::menu_sensitivity;
  xe::be<float>* zoom_level = kernel_memory()->TranslateVirtual<xe::be<float>*>(
      supported_builds[game_build_].customization_screen_zoom_level_addr);

  // this is absoloute player rotation, this isn't fixed to the player
  // customization screens
  xe::be<float>* player_x_sin =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(player + 0x40);

  xe::be<float>* player_x_cos =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(player + 0x38);

  float min_zoom = 0.75f;
  float max_zoom = 3.f;
  if (vehicle_status == 1) {  // maybe read of rims jobs flags instead?
                              // 828522D5,828527FD, 82852A91, 82852D25
    min_zoom = 3.f;
    max_zoom = 9.f;
    xe::be<uint32_t>* rims_jobs_vehicle_pointer = multi_pointer(
        supported_builds[game_build_].rims_jobs_address_ptr, {0x20, 0x98});
    if (!rims_jobs_vehicle_pointer || *rims_jobs_vehicle_pointer == NULL) {
      return false;
    }
    player_x_sin = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        *rims_jobs_vehicle_pointer + 0x40);
    player_x_cos = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        *rims_jobs_vehicle_pointer + 0x38);
    mousex = std::clamp(mousex, -24.f,
                        24.f);  // random value to limit mouse delta otherwise
                                // the cars starts freaking out
  }

  float zoom = *zoom_level;
  zoom = RadianstoDegree(zoom);  // probably not really in radians..
  zoom -= input_state.mouse.wheel_delta / 7.5f;
  zoom += (input_state.mouse.y_delta / 8.f) * (float)cvars::menu_sensitivity;
  *zoom_level = std::clamp(DegreetoRadians(zoom), min_zoom, max_zoom);
  float x = atan2(*player_x_sin, *player_x_cos);
  x = RadianstoDegree(x);

  x += mousex;
  if (x > 180.0f) {
    x -= 360.0f;
  } else if (x < -180.0f) {
    x += 360.0f;
  }
  x = DegreetoRadians(x);
  *player_x_sin = sin(x);
  *player_x_cos = cos(x);
  return true;
}

bool SaintsRow1Game::CantSwitchWeapons() {
  if (player == NULL) {
    return false;
  }

  uint32_t onfoot_bitmask =
      *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player + 0x1178);

  uint32_t driving_bitmask =
      *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player + 0x117C);

  uint32_t vehicle =
      *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player + 0x9C0);
  if (!((onfoot_bitmask & 1) != 0 || (driving_bitmask & 1) != 0 && vehicle)) {
    return true;
  }

  return false;
}

// bool SaintsRow1Game::CantSwitchWeapons() {
//   if (isAnimStatus(animstatus::DEAD) || isAnimStatus(animstatus::JUMPING) ||
//       isAnimStatus(animstatus::RAGDOLL) ||
//       IsPlayerStatus1(playerstatus1::BUSY) ||
//       IsPlayerStatus1(playerstatus1::SPRINTING) ||
//       IsPlayerStatus1(playerstatus1::STANDINGUP) ||
//       IsPlayerStatus1(playerstatus1::JUMPING1))
//     return true;
//   else
//     return false;
// }

void SaintsRow1Game::WeaponWheelScrollWheel(RawInputState& input_state) {
  if (player == NULL) {
    return;
  }
  if (isMP() && CantSwitchWeapons()) {
    return;
  }

  auto* weapon_slot = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].weapon_wheel_slot_address);
  xe::be<uint32_t>* current_weapon =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player + 0xDDC);

  if (input_state.mouse.wheel_delta) {
    SelectableWeaponsHack();  // controls if we can use the 4
                              // un-usable weapons,
    // 827D0484, 827D04C0, 827D04D4, 827D04E8, calling it here
    // doesn't work unless we at least open the wheel in a
    // vehicle once, then it works expect for melees?
    // call_argless_function(0x822ADD48);
    int16_t original_slot = static_cast<int16_t>(*weapon_slot);
    int16_t slot = original_slot;

    uint32_t old_weapon = *current_weapon;
    bool weapon_switched = false;

    int direction = (input_state.mouse.wheel_delta > 0) ? 1 : -1;

    if (cvars::swap_wheel) {
      direction = -direction;
    }

    for (int attempts = 0; attempts < 8; ++attempts) {
      slot = (slot + direction + 8) % 8;

      *weapon_slot = static_cast<uint8_t>(slot);

      call_argless_function(supported_builds[game_build_]
                                .change_weapon_function_addr);  // set weapon

      if (*current_weapon != old_weapon) {
        weapon_switched = true;
        break;
      }
    }
    if (!weapon_switched) {
      *weapon_slot = static_cast<uint8_t>(original_slot);
    }
  }
}

bool SaintsRow1Game::inMapScreen() {
  auto* pause_screen = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].pause_screen_section_address);

  auto* map_usable = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].current_diversion_type_addr);

  if ((*pause_screen == 26 || *map_usable == 210) && isPaused()) {
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

void SaintsRow1Game::call_argless_function(uint32_t function_address) {
  XThread* current_thread = XThread::GetCurrentThread();
  if (!current_thread) {
    return;
  }
  kernel_state()->processor()->Execute(current_thread->thread_state(),
                                       function_address);
  return;
}

void SaintsRow1Game::reload_players_weapon() {
  XThread* current_thread = XThread::GetCurrentThread();
  if (!current_thread || !player) {
    return;
  }

  uint32_t check_value =
      *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player + 780);
  if (check_value != 0) {
    return;
  }

  uint32_t can_we_bitmask =
      *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player + 0x1180);
  if ((((1 << 12) & can_we_bitmask) == 0)) {
    return;
  }

  current_thread->thread_state()->context()->r[3] = player;
  kernel_state()->processor()->Execute(
      current_thread->thread_state(),
      supported_builds[game_build_].reload_players_weapon_addr);
  return;
}

std::string SaintsRow1Game::ChooseBinds() {
  wheel_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].weapon_wheel_address);
  vehicle_status = *kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].vehicle_address);

  if (*wheel_status == 1 || isPaused()) {
    return "Default";
  }
  if (vehicle_status && vehicle_status == 1) {
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

bool SaintsRow1Game::isAnimStatus(uint8_t type) {
  if (player == NULL) {
    return false;
  }
  auto* anim_status =
      kernel_memory()->TranslateVirtual<uint8_t*>(player + 0x1FF);

  if (*anim_status == type) {
    return true;
  } else {
    return false;
  }
}

bool SaintsRow1Game::IsPlayerStatus1(uint32_t type) {
  if (player == NULL) {
    return false;
  }
  auto* player_status1 =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player + 0x1180);

  if (*player_status1 == type) {
    return true;
  } else {
    return false;
  }
}

void SaintsRow1Game::SelectableWeaponsHack() {
  /*Opening weapon wheel does call limit_weapons_function_addr correctly, so
   * skip our attempt at re-creating it.*/
  auto* food_selector =
      kernel_memory()
          ->TranslateVirtual<xe::be<uint32_t>*>(  // possibily signed? for our
                                                  // purpose
                                                  // it doesn't matter.
              supported_builds[game_build_].food_wheel_object_address);
  auto* food_slot = kernel_memory()->TranslateVirtual<xe::be<int32_t>*>(
      supported_builds[game_build_].food_wheel_slot_address);
  if (*food_selector != 0) {
    *food_selector = 0;
  }
  if (*food_slot != -1) {
    *food_slot = -1;
  }
  if (*wheel_status) {
    return;
  }
  call_argless_function(
      supported_builds[game_build_]
          .limit_weapons_function_addr);  // Ideally this should have done
                                          // everything that this function does,
                                          // but calling it doesn't seem to do
                                          // the job fully??
  auto* melee_slot = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].allowable_weapons_melee_array);
  auto* shotgun_slot = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].allowable_weapons_melee_array + 0x3C);
  auto* ar_slot = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].allowable_weapons_melee_array + 0x50);
  auto* rpg_slot = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].allowable_weapons_melee_array + 0x64);

  if (isAnimStatus(animstatus::DRIVING) && vehicle_status) {
    *melee_slot = 1;
    *shotgun_slot = 1;
    *ar_slot = 1;
    *rpg_slot = 1;
  } else if (isAnimStatus(animstatus::PASSANGER) && vehicle_status) {
    *melee_slot = 1;
    *shotgun_slot = 0;
    *ar_slot = 0;
    *rpg_slot = 0;
  } else if (vehicle_status == 0) {
    *melee_slot = 0;
    *shotgun_slot = 0;
    *ar_slot = 0;
    *rpg_slot = 0;
  }
}

void SaintsRow1Game::WeaponSwitchHandler(uint32_t user_index,
                                         RawInputState& input_state,
                                         X_INPUT_STATE* out_state, int weapon,
                                         uint16_t buttons) {
  if (weapon == 10) {
    reload_players_weapon();
    return;
  }

  if (isPaused() || isAnimStatus(animstatus::DEAD)) {
    return;
  }
  // This probably works fine but might need more testing, and It'd be more
  // accurate to the SR2 PC port,BUT I prefer being able to switch weapons while
  // sprinting, make this part of a WeaponSwitchHandler cvar in the future?
  if (isMP() && CantSwitchWeapons()) {
    return;
  }
  auto* weapon_slot = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].weapon_wheel_slot_address);
  SelectableWeaponsHack();
  if (weapon) {
    *weapon_slot = std::clamp(weapon - 1, 0, 7);
    call_argless_function(
        supported_builds[game_build_].change_weapon_function_addr);
  }
}

constexpr double deg_to_rad(double degrees) { return degrees * M_PI / 180.0; }

constexpr double rad_to_deg(double radians) { return radians * 180.0 / M_PI; }

// can't be in class because it'd pass in `this`
void print_x_axis_midhook(PPCContext* context, void* arg0, void* arg1) {
  return;
  context->f[30] = context->f[30] + (mouse_x_ld / 1250.0);
  mouse_x_ld = 0;
}

void x_addition_hook(PPCContext* context, void* arg0, void* arg1) {
  if (cvars::internal_hook == false) {
    return;
  }
  double divider = 15.0;
  if (SaintsRow1Game::current_instance_) {
    xe::be<float>* current_fov =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(
            supported_builds[SaintsRow1Game::current_instance_->game_build_]
                .current_fov_address);
    float fov = *current_fov;
    if (fov < 60.f) {
      fov = 60.f / fov;
      divider = divider * fov;
    }
  }

  double degrees = rad_to_deg(context->f[27]);
  degrees = degrees + ((mouse_x_ld / divider) * (float)cvars::sensitivity);
  context->f[27] = context->f[27] + deg_to_rad(degrees);
  mouse_x_ld = 0;
}

void SaintsRow1Game::MidHookInit() {
  if (midhook_status == HOOKED || !cvars::internal_hook) {
    return;
  }
  SaintsRow1Game::current_instance_ = this;

  if (!supported_builds[game_build_].radian_x_axis_midhook_addr1) {
    return;
  }
  current_instance_ = this;

  xe::cpu::ppc::RegisterMidHookASM(
      supported_builds[game_build_].radian_x_axis_midhook_addr1,
      x_addition_hook);

  midhook_status = HOOKED;
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
