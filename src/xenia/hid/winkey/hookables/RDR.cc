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
DECLARE_double(right_stick_hold_time_workaround);

const uint32_t kTitleIdRedDeadRedemption = 0x5454082B;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t check_addr;
  uint32_t check_value;
  uint32_t base_address;
  uint32_t x_offset;
  uint32_t y_offset;
  uint32_t z_offset;
  uint32_t auto_center_strength_offset;
  uint32_t mounting_center_address;
  uint32_t cover_base_address;
  uint32_t x_cover_offset;
  uint32_t y_cover_offset;
  uint32_t mounted_x_offset_from_cover;
  uint32_t cam_type_address;
  uint32_t cam_type_offset;
  uint32_t pause_flag_address;
  uint32_t fovscale_base_address;
  uint32_t weapon_wheel_status;
};

std::map<RedDeadRedemptionGame::GameBuild, GameBuildAddrs> supported_builds{
    {RedDeadRedemptionGame::GameBuild::RedDeadRedemption_GOTY_Disk1,
     {"12.0", 0x82010BEC, 0x7A3A5C72, 0x8309C298, 0x460, 0x45C, 0x458, 0x3EC,
      0xBE684000, 0x820D6A8C, 0xF1F, 0x103F, 0x5680, 0x820D68E8, 0x794B,
      0x82F79E77, 0xBE67B80C, 0xBEA580F3}},
    {RedDeadRedemptionGame::GameBuild::RedDeadRedemption_GOTY_Disk2,
     {"12.0", 0x82010C0C, 0x7A3A5C72, 0x8309C298, 0x460, 0x45C, 0x458, 0x3EC,
      0xBE641960, NULL, NULL, NULL}},
    {RedDeadRedemptionGame::GameBuild::RedDeadRedemption_Original_TU0,
     {"1.0", NULL, NULL, 0x830641D8, 0x460, 0x45C, 0x458, 0x3EC, 0xBE65B73C,
      0xBE661AC8, 0x1A0, 0x2C0, NULL, 0xBE68A060, 0xB, 0x82F49B73,
      0xBE64CEAC}}};

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

  auto now = std::chrono::steady_clock::now();
  auto elapsed_x = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_movement_time_x_)
                       .count();
  auto elapsed_y = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_movement_time_y_)
                       .count();

  XThread* current_thread = XThread::GetCurrentThread();
  if (!current_thread) {
    return false;
  }
  if (supported_builds[game_build_].pause_flag_address != NULL) {
    xe::be<uint8_t>* isPaused =
        kernel_memory()->TranslateVirtual<xe::be<uint8_t>*>(
            supported_builds[game_build_].pause_flag_address);
    if (isPaused && *isPaused >= 4) {
      return false;
    }
  }

  xe::be<uint32_t>* base_address =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
          supported_builds[game_build_].base_address);

  if (!base_address || *base_address == NULL) {
    // Not in game
    return false;
  }

  float divisor;
  if (supported_builds[game_build_].fovscale_base_address != NULL) {
    xe::be<uint32_t>* fovscale_address =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].fovscale_base_address);
    xe::be<uint32_t> fovscale_address_result = +0xD0;
    xe::be<float>* fovscale = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        fovscale_address_result);

    float fov = *fovscale;
    if (fov <= 0.5f) {
      fov = 0.5f;
    }
    divisor = 1000.f * fov;
  } else
    divisor = 1000.f;

  if (supported_builds[game_build_].cover_base_address != NULL) {
    xe::be<uint32_t>* cam_type_result =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].cam_type_address);
    xe::be<uint32_t> cam_byte_read =
        *cam_type_result + supported_builds[game_build_].cam_type_offset;
    auto* cam_type = kernel_memory()->TranslateVirtual<uint8_t*>(cam_byte_read);
    if (cam_type && *cam_type == 9) {
      xe::be<uint32_t>* cover_base =
          kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              supported_builds[game_build_].cover_base_address);
      xe::be<uint32_t> x_cover_address =
          *cover_base + supported_builds[game_build_].x_cover_offset;
      xe::be<uint32_t> y_cover_address =
          *cover_base + supported_builds[game_build_].y_cover_offset;
      xe::be<float>* radian_x_cover =
          kernel_memory()->TranslateVirtual<xe::be<float>*>(x_cover_address);
      float camX = *radian_x_cover;
      xe::be<float>* radian_y_cover =
          kernel_memory()->TranslateVirtual<xe::be<float>*>(y_cover_address);
      float camY = *radian_y_cover;
      camX -=
          ((input_state.mouse.x_delta * (float)cvars::sensitivity) / divisor);
      camY -=
          ((input_state.mouse.y_delta * (float)cvars::sensitivity) / divisor);
      *radian_x_cover = camX;

      *radian_y_cover = camY;
      if (supported_builds[game_build_].mounted_x_offset_from_cover != NULL) {
        xe::be<float>* radian_x_mounted =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(
                x_cover_address -
                supported_builds[game_build_].mounted_x_offset_from_cover);
        *radian_x_mounted = camX;
      }
    }
  }

  xe::be<uint32_t> x_address =
      *base_address - supported_builds[game_build_].x_offset;
  xe::be<uint32_t> y_address =
      *base_address - supported_builds[game_build_].y_offset;
  xe::be<uint32_t> z_address =
      *base_address - supported_builds[game_build_].z_offset;
  xe::be<uint32_t> auto_center_strength_address =
      *base_address - supported_builds[game_build_].auto_center_strength_offset;

  if (supported_builds[game_build_].cam_type_address != NULL) {
    xe::be<uint32_t>* cam_type_result =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].cam_type_address);
    xe::be<uint32_t> cam_byte_read =
        *cam_type_result + supported_builds[game_build_].cam_type_offset;
    auto* cam_type = kernel_memory()->TranslateVirtual<uint8_t*>(cam_byte_read);
    if (cam_type &&
        (*cam_type == 10 || *cam_type == 13)) {  // Carriage & Mine Cart Cam
      x_address -= 0x810;
      y_address -= 0x810;
      z_address -= 0x810;
      auto_center_strength_address = x_address + 0x74;
    }
  }

  xe::be<float>* degree_x_act =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(x_address);

  xe::be<float>* degree_y_act =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(y_address);

  xe::be<float>* degree_z_act =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(z_address);

  xe::be<float>* auto_center_strength_act =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(
          auto_center_strength_address);

  float auto_center_strength = *auto_center_strength_act;

  float degree_x = *degree_x_act;
  float degree_y = *degree_y_act;
  float degree_z = *degree_z_act;

  // Calculate the horizontal and vertical angles
  float hor_angle = atan2(degree_z, degree_x);
  float vert_angle = asin(degree_y);

  hor_angle += ((input_state.mouse.x_delta * (float)cvars::sensitivity) /
                divisor);  // X-axis delta
  vert_angle = std::clamp(
      vert_angle -
          ((input_state.mouse.y_delta * (float)cvars::sensitivity) / divisor),
      -static_cast<float>(M_PI / 2.0f), static_cast<float>(M_PI / 2.0f));

  // Calculate 3D camera vector |
  // https://github.com/isJuhn/KAMI/blob/master/KAMI.Core/Cameras/HVVecCamera.cs
  degree_x = cos(hor_angle) * cos(vert_angle);
  degree_z = sin(hor_angle) * cos(vert_angle);
  degree_y = sin(vert_angle);

  if (degree_y > 0.7153550260f)
    degree_y = 0.7153550260f;
  else if (degree_y < -0.861205390f)
    degree_y = -0.861205390f;
  if (supported_builds[game_build_].weapon_wheel_status &&
      *kernel_memory()->TranslateVirtual<uint8_t*>(
          supported_builds[game_build_].weapon_wheel_status) == 2) {
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
  } else {
    *degree_x_act = degree_x;
    *degree_y_act = degree_y;
    *degree_z_act = degree_z;
  }
  if (supported_builds[game_build_].auto_center_strength_offset != NULL &&
      auto_center_strength <= 1.f &&
      (input_state.mouse.x_delta != 0 || input_state.mouse.y_delta != 0)) {
    auto_center_strength += 0.15f;  // Maybe setting it to 1.f is better, I'm
                                    // just hoping += 0.x makes it smoother..
    if (auto_center_strength > 1.f) {
      auto_center_strength = 1.f;
    }
    *auto_center_strength_act = auto_center_strength;
  }
  if (supported_builds[game_build_].mounting_center_address != NULL &&
      (input_state.mouse.x_delta != 0 || input_state.mouse.y_delta != 0)) {
    xe::be<uint32_t>* mounting_center_pointer =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].mounting_center_address);
    xe::be<uint32_t> mounting_center_final = *mounting_center_pointer + 0x1F00;
    auto* mounting_center =
        kernel_memory()->TranslateVirtual<uint8_t*>(mounting_center_final);
    if (*mounting_center != 0) *mounting_center = 0;
  }
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