/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/EarthDefenseForce.h"

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
DECLARE_double(fov_sensitivity);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);

const uint32_t kTitleIDEDF2017 = 0x445007D3;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  uint32_t title_id;
  uint32_t player_address;
};

std::map<EarthDefenseForceGame::GameBuild, GameBuildAddrs> supported_builds{
    {EarthDefenseForceGame::GameBuild::EarthDefenseForce2017,
     {kTitleIDEDF2017, 0x825786AC}}};

EarthDefenseForceGame::~EarthDefenseForceGame() = default;

bool EarthDefenseForceGame::IsGameSupported(GameVersion title_version) {
  auto title_id = kernel_state()->title_id();
  if (title_id != kTitleIDEDF2017) {
    return false;
  }

  for (auto& build : supported_builds) {
    if (title_id == build.second.title_id) {
      game_build_ = build.first;
      return true;
    }
  }

  return true;
}

float EarthDefenseForceGame::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float EarthDefenseForceGame::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}

bool EarthDefenseForceGame::DoHooks(uint32_t user_index,
                                    RawInputState& input_state,
                                    X_INPUT_STATE* out_state) {
  if ((!input_state.mouse.x_delta && !input_state.mouse.y_delta &&
       !input_state.mouse.wheel_delta))
    return false;

  uint32_t player = *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
      supported_builds[game_build_].player_address);
  printf("player 0x%X\n", player);
  if (player == NULL) return false;

  uint32_t cambase =
      *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player + 0x2FC);
  printf("cambase 0x%X\n", cambase);
  if (cambase == NULL) return false;

  xe::be<float>* radian_x =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(cambase + 0x524);

  xe::be<float>* radian_y =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(cambase + 0x520);

  xe::be<float>* radian_x_cur =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(cambase + 0x524 - 0x10);

  xe::be<float>* radian_y_cur =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(cambase + 0x520 - 0x10);

  float degree_x = RadianstoDegree(*radian_x);
  float degree_y = RadianstoDegree(*radian_y);

  float degree_x_cur = RadianstoDegree(*radian_x_cur);
  float degree_y_cur = RadianstoDegree(*radian_y_cur);

  float divisor = 5.f;

  // X-axis = 0 to 360
  float x_multiplier = cvars::invert_x ? 1.f : -1.f;
  float y_multiplier = cvars::invert_y ? -1.f : 1.f;

  degree_x += x_multiplier * (input_state.mouse.x_delta / divisor) *
              (float)cvars::sensitivity;
  *radian_x = DegreetoRadians(degree_x);
  degree_y += y_multiplier * (input_state.mouse.y_delta / divisor) *
              (float)cvars::sensitivity;
  *radian_y =
      std::clamp(DegreetoRadians(degree_y), -1.256637096f, 1.256637096f);
  static volatile bool cur_allow = true;
  if (cur_allow) {
    *radian_x_cur = DegreetoRadians(
        degree_x_cur += x_multiplier * (input_state.mouse.x_delta / divisor) *
                        (float)cvars::sensitivity);
    *radian_y_cur = std::clamp(
        DegreetoRadians(degree_y_cur += y_multiplier *
                                        (input_state.mouse.y_delta / divisor) *
                                        (float)cvars::sensitivity),
        -1.256637096f, 1.256637096f);
  }
  // printf("cur_allow %p\n", &cur_allow);

  return true;
}

std::string EarthDefenseForceGame::ChooseBinds() { return "Default"; }

bool EarthDefenseForceGame::ModifierKeyHandler(uint32_t user_index,
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

void EarthDefenseForceGame::WeaponSwitchHandler(uint32_t user_index,
                                                RawInputState& input_state,
                                                X_INPUT_STATE* out_state,
                                                int weapon, uint16_t buttons) {}

}  // namespace winkey
}  // namespace hid
}  // namespace xe