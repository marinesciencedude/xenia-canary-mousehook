/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/RDR.h"

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

const uint32_t kTitleIdRedDeadRedemption = 0x5454082B;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t check_addr;
  uint32_t check_value;
  uint32_t x_address;
  uint32_t y_offset;
  uint32_t z_offset;
  uint32_t auto_center_strength_offset;
  uint32_t mounting_center_address;
};

std::map<RedDeadRedemptionGame::GameBuild, GameBuildAddrs> supported_builds{
    {RedDeadRedemptionGame::GameBuild::RedDeadRedemption_GOTY_Disk1,
     {"12.0", 0x82010BEC, 0x7A3A5C72, 0xBE674BE0, 0x4, 0x8, 0x74, 0xBE665F00}},
    {RedDeadRedemptionGame::GameBuild::RedDeadRedemption_GOTY_Disk2,
     {"12.0", 0x82010C0C, 0x7A3A5C72, 0xBE64CFD0, 0x4, 0x8, 0x74, 0xBE641960}},
    {RedDeadRedemptionGame::GameBuild::RedDeadRedemption_Original_TU0,
     {"1.0", NULL, NULL, 0xBE63D6D0, 0x4, 0x8, 0x74, NULL}}};

RedDeadRedemptionGame::~RedDeadRedemptionGame() = default;

bool RedDeadRedemptionGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdRedDeadRedemption) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    if (build.second.check_addr != NULL) {
      // Required due to GOTY disks sharing same version.
      auto* check_addr_ptr =
          kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              build.second.check_addr);
      if (*check_addr_ptr == build.second.check_value) {
        game_build_ = build.first;
        return true;
      }
    } else if (current_version == build.second.title_version) {
      game_build_ = build.first;
      return true;
    }
  }

  return false;
}

float RedDeadRedemptionGame::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float RedDeadRedemptionGame::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}

bool RedDeadRedemptionGame::DoHooks(uint32_t user_index,
                                    RawInputState& input_state,
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

  xe::be<float>* degree_x_act =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].x_address);
  xe::be<float>* degree_y_act =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].x_address +
          supported_builds[game_build_].y_offset);
  xe::be<float>* degree_z_act =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].x_address +
          supported_builds[game_build_].z_offset);
  xe::be<float>* auto_center_strength_act =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          supported_builds[game_build_].x_address +
          supported_builds[game_build_].auto_center_strength_offset);
  auto* mounting_center = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].mounting_center_address);

  float auto_center_strength = *auto_center_strength_act;

  float degree_x = *degree_x_act;
  float degree_y = *degree_y_act;
  float degree_z = *degree_z_act;

  // Calculate the horizontal and vertical angles
  float hor_angle = atan2(degree_z, degree_x);
  float vert_angle = asin(degree_y / (float)cvars::sensitivity);

  hor_angle += (input_state.mouse.x_delta / 1000.f);  // X-axis delta
  vert_angle = std::clamp(vert_angle - (input_state.mouse.y_delta / 1000.f),
                          -static_cast<float>(M_PI / 2.0f),
                          static_cast<float>(M_PI / 2.0f));

  // Calculate 3D camera vector |
  // https://github.com/isJuhn/KAMI/blob/master/KAMI.Core/Cameras/HVVecCamera.cs
  degree_x = cos(hor_angle) * cos(vert_angle) * (float)cvars::sensitivity;
  degree_z = sin(hor_angle) * cos(vert_angle) * (float)cvars::sensitivity;
  degree_y = sin(vert_angle) * (float)cvars::sensitivity;

  *degree_x_act = degree_x;
  *degree_y_act = degree_y;
  *degree_z_act = degree_z;

  if (supported_builds[game_build_].auto_center_strength_offset != NULL &&
      auto_center_strength <= 1.f &&
      (input_state.mouse.x_delta != 0 || input_state.mouse.y_delta != 0)) {
    auto_center_strength += 0.5f;  // Maybe setting it to 1.f is better, I'm
                                   // just hoping += 0.5 makes it smoother..
    if (auto_center_strength > 1.f) {
      auto_center_strength = 1.f;
    }
    *auto_center_strength_act = auto_center_strength;
  }
  if (supported_builds[game_build_].mounting_center_address != NULL &&
      *mounting_center != 0 &&
      (input_state.mouse.x_delta != 0 || input_state.mouse.y_delta != 0))
    *mounting_center = 0;
  return true;
}

std::string RedDeadRedemptionGame::ChooseBinds() { return "Default"; }

bool RedDeadRedemptionGame::ModifierKeyHandler(uint32_t user_index,
                                               RawInputState& input_state,
                                               X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe