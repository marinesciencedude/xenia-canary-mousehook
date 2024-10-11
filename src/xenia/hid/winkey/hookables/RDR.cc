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
DECLARE_double(fov_sensitivity);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);
DECLARE_double(right_stick_hold_time_workaround);
DECLARE_bool(rdr_turbo_gallop_horse);
DECLARE_bool(rdr_snappy_wheel)

    const uint32_t kTitleIdRedDeadRedemption = 0x5454082B;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t check_addr;
  uint32_t check_value;
  uint32_t base_address;                 // pointer points to base camera,
  uint32_t x_offset;                     // Carriage is -0x810 of these
  uint32_t y_offset;                     // Carriage is -0x810 of these
  uint32_t z_offset;                     // Carriage is -0x810 of these
  uint32_t auto_center_strength_offset;  // defaultCamAutoPos // calcucalted
                                         // from base_address
  uint32_t mounting_center_address;      // mountingCamAutoPos
  uint32_t cover_base_address;
  uint32_t x_cover_offset;
  uint32_t y_cover_offset;
  uint32_t mounted_base_address;  // MoveTurretCam
  uint32_t mounted_x_offset;
  uint32_t cam_type_address;  // rdrCamTypeMemPos
  uint32_t cam_type_offset;
  uint32_t pause_flag_address;         // gamePausePos
  uint32_t fovscale_base_address;      // rdrZoomMemPos
  uint32_t fovscale_offset;            // unused for now..
  uint32_t weapon_wheel_base_address;  // rdrMenuTypeMemPos
  uint32_t weapon_wheel_offset;
  uint32_t cinematicCam_address;
};

struct GameBuildAddrs supported_builds[6] = {
    {"",   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    // RedDeadRedemption_GOTY_Disk1
    {"12.0",     0x82010BEC, 0x7A3A5C72, 0x8309C298, 0x460,      0x45C,
     0x458,      0x3EC,      0xBE684000, 0x820D6A8C, 0xF1F,      0x103F,
     0xBBC67E24, 0x2B0,      0x820D68E8, 0x794B,     0x82F79E77, 0xBE67B80C,
     0xD0,       0x82F7B450, 0xF3,       0x7049E69C},

    // RedDeadRedemption_GOTY_Disk2
    {"12.0",     0x82010C0C, 0x7A3A5C72, 0x8309C298, 0x460,      0x45C,
     0x458,      0x3EC,      0xBE63AB24, 0xBE65C7FC, 0x1A0,      0x2C0,
     0xBE642900, 0x2B0,      0x8305D684, 0x4D0D4B,   0x82F79E77, 0xBE65780C,
     0xD0,       0x82F7B450, 0xF3,       0x7049E69C},

    // RedDeadRedemption_Original_TU0
    {"1.0",      NULL,       NULL,       0x830641D8, 0x460,      0x45C,
     0x458,      0x3EC,      0xBE65B73C, 0xBE661AC8, 0x1A0,      0x2C0,
     0xBBC5FD14, 0x2B0,      0xBE68A060, 0xB,        0x82F49B73, 0xBE64CEAC,
     0xD0,       0x82F4B0E0, 0xF3,       0x7049E69C},

    // RedDeadRedemption_Original_TU9
    {"1.0.9",    NULL,       NULL,       0x8305DBE8, 0x460,      0x45C,
     0x458,      0x3EC,      0xBE69827C, 0xBE696608, 0x1A0,      0x2C0,
     0xBBC63E24, 0x2B0,      0xBE6BAB60, 0xB,        0x82F49EB7, 0xBE685CEC,
     0xD0,       0x82F4B660, 0xF3,       0x7049E69C},

    // RedDeadRedemption_UndeadNightmare_Standalone_TU4
    {"4.0",      NULL,       NULL,       0x8309AF88, 0x460,      0x45C,
     0x458,      0x3EC,      0xBE6430A4, 0xBE65B88C, 0x1A0,      0x2C0,
     0xBBC67E3C, 0x2B0,      0xBE685260, 0xB,        0x82F79E77, 0xBE64F80C,
     0xD0,       0x82F7B450, 0xF3,       0x7049E69C}};

RedDeadRedemptionGame::~RedDeadRedemptionGame() = default;
uint32_t RedDeadRedemptionGame::cached_carriage_x_address = 0;
uint32_t RedDeadRedemptionGame::cached_carriage_y_address = 0;
uint32_t RedDeadRedemptionGame::cached_carriage_z_address = 0;
uint32_t RedDeadRedemptionGame::cached_auto_center_strength_address_carriage =
    0;
static uint32_t cached_mounting_center_final = 0;
static uint32_t cached_cover_center_final = 0;
bool RedDeadRedemptionGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdRedDeadRedemption) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (int i = 0; i < (sizeof(supported_builds) / sizeof(supported_builds[0]));
       i++) {
    if (supported_builds[i].check_addr != NULL) {
      auto* check_addr_ptr =
          kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              supported_builds[i].check_addr);
      if (*check_addr_ptr == supported_builds[i].check_value) {
        game_build_ = static_cast<RedDeadRedemptionGame::GameBuild>(i);
        return true;
      }
    } else if (current_version == supported_builds[i].title_version) {
      game_build_ = static_cast<RedDeadRedemptionGame::GameBuild>(i);
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

  if (game_build_ < 0 ||
      game_build_ >= sizeof(supported_builds) / sizeof(supported_builds[0])) {
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
  if (IsPaused()) return false;

  xe::be<uint32_t>* base_address =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
          supported_builds[game_build_].base_address);

  if (!base_address || *base_address == NULL) {
    // Not in game
    return false;
  }
  if (IsCinematicTypeEnabled()) {
    const float invert_x_multiplier = cvars::invert_x ? 1.0f : -1.0f;
    const float invert_y_multiplier = cvars::invert_y ? 1.0f : -1.0f;
    if (!IsPaused()) {
      // Perform pattern scan to find carriage addresses if not already cached
      if (cached_carriage_x_address == 0) {
        // Perform pattern scan
        uint32_t start_address = 0xBA000000;  // Adjust as necessary
        uint32_t end_address = 0xBF000000;    // Adjust as necessary

        std::vector<uint8_t> pattern = {0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD,
                                        0xCD, 0xCD, 0xBE, 0xCC, 0xCC, 0xCC,
                                        0x00, 0x00, 0x03, 0xB0, 0xBE, 0xCC,
                                        0xCC, 0xCC, 0x00, 0x00, 0x00, 0x50};

        uint32_t pattern_address = FindPatternWithWildcardAddress(
            start_address, end_address, pattern, "Carriage 3D Camera");

        if (pattern_address != 0) {
          // Calculate the base address for carriage x, y, z
          uint32_t carriage_base_address = pattern_address - 0x77;

          // Cache the addresses
          cached_carriage_x_address = carriage_base_address;
          cached_carriage_y_address = carriage_base_address + 0x04;
          cached_carriage_z_address = carriage_base_address + 0x08;
          cached_auto_center_strength_address_carriage =
              carriage_base_address + 0x74;
        }
      }
      if (cached_mounting_center_final == 0 || cached_cover_center_final == 0) {
        // Pattern for 'shouldAutoAlignBehind'
        std::vector<uint8_t> pattern = {
            0x73, 0x68, 0x6F, 0x75, 0x6C, 0x64, 0x41, 0x75, 0x74, 0x6F, 0x41,
            0x6C, 0x69, 0x67, 0x6E, 0x42, 0x65, 0x68, 0x69, 0x6E, 0x64};
        uint32_t start_address = 0xBA000000;
        uint32_t end_address = 0xBF000000;
        uint32_t pattern_address = FindPatternWithWildcardAddress(
            start_address, end_address, pattern, "shouldAutoAlignBehind");
        if (pattern_address != 0) {
          cached_mounting_center_final = pattern_address - 0x3F;
          cached_cover_center_final = pattern_address - 0x99F;
        }
      }
    }

    // static uint32_t saved_fovscale_address = 0;
    static float divisor = 850.5f;
    if (supported_builds[game_build_].fovscale_base_address != NULL) {
      xe::be<uint32_t>* fovscale_address =
          kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              supported_builds[game_build_].fovscale_base_address);
      xe::be<float>* fovscale =
          kernel_memory()->TranslateVirtual<xe::be<float>*>(
              *fovscale_address +
              supported_builds[game_build_].fovscale_offset);
      xe::be<uint16_t>* fovscale_sanity =
          kernel_memory()->TranslateVirtual<xe::be<uint16_t>*>(
              *fovscale_address +
              supported_builds[game_build_].fovscale_offset - 0x38);

      if (*fovscale_sanity == 0x0000) {
        float fov = *fovscale;
        if (fov <= 0.5f || fov > 35.f) {
          fov = 1.f;
        }
        const float a = (float)cvars::fov_sensitivity;
        if (fov >= 0.96f) {
          fov = a * fov + (1 - a) * (fov * fov);
        }

        divisor = 850.5f * fov;

        // printf("fov: %f divisor: %f\n", fov, divisor);
      }

      else {
        divisor = 850.5f;
      }
    }
    if (supported_builds[game_build_].cover_base_address != NULL) {
      uint8_t cam_type = GetCamType();
      if ((cam_type && cam_type == 9 && !IsWeaponWheelShown()) ||
          ((cam_type == 7 || cam_type == 6) &&
           supported_builds[game_build_].mounted_base_address != NULL)) {
        xe::be<uint32_t>* cover_base =
            kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
                supported_builds[game_build_].cover_base_address);
        xe::be<float>* radian_x_ptr = nullptr;
        xe::be<float>* radian_y_ptr = nullptr;

        if (cam_type == 9) {
          // Cover mode
          xe::be<uint32_t> x_cover_address =
              *cover_base + supported_builds[game_build_].x_cover_offset;
          xe::be<uint32_t> y_cover_address =
              *cover_base + supported_builds[game_build_].y_cover_offset;
          radian_x_ptr = kernel_memory()->TranslateVirtual<xe::be<float>*>(
              x_cover_address);
          radian_y_ptr = kernel_memory()->TranslateVirtual<xe::be<float>*>(
              y_cover_address);

          if (supported_builds[game_build_].mounting_center_address != NULL) {
            xe::be<uint32_t>* cover_center_pointer =
                kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
                    supported_builds[game_build_].mounting_center_address);
            xe::be<uint32_t> cover_center_final =
                *cover_center_pointer + 0x15a0;
            auto* cover_sanity =
                kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
                    cover_center_final + 0x9A0);
            static uint32_t shoul = 0x73686F75;
            auto* cover_center =
                kernel_memory()->TranslateVirtual<uint8_t*>(cover_center_final);

            if (*cover_center != 0 && IsMouseMoving(input_state) &&
                *cover_sanity == shoul) {
              *cover_center = 0;
            } else if (cached_cover_center_final != 0 &&
                       *cover_sanity != shoul) {
              // Use cached address if sanity check fails
              cover_center_final = cached_cover_center_final;
              cover_center = kernel_memory()->TranslateVirtual<uint8_t*>(
                  cover_center_final);
              if (*cover_center != 0 && IsMouseMoving(input_state)) {
                *cover_center = 0;
              }
            }
          }
        } else if ((cam_type == 7 || cam_type == 6) &&
                   supported_builds[game_build_].mounted_base_address != NULL) {
          // Cannon or turret mode
          xe::be<uint32_t>* mounted_base =
              kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
                  supported_builds[game_build_].mounted_base_address);
          xe::be<uint32_t> y_cover_address =
              *cover_base + supported_builds[game_build_].y_cover_offset;
          xe::be<uint32_t> x_mounted_cover_address =
              *mounted_base + supported_builds[game_build_].mounted_x_offset;

          radian_x_ptr = kernel_memory()->TranslateVirtual<xe::be<float>*>(
              x_mounted_cover_address);
          radian_y_ptr = kernel_memory()->TranslateVirtual<xe::be<float>*>(
              y_cover_address);
        }

        if (radian_x_ptr && radian_y_ptr) {
          float camX = *radian_x_ptr;
          float camY = *radian_y_ptr;

          camX += invert_x_multiplier *
                  ((input_state.mouse.x_delta * (float)cvars::sensitivity) /
                   divisor);
          camY += invert_y_multiplier *
                  ((input_state.mouse.y_delta * (float)cvars::sensitivity) /
                   divisor);

          *radian_x_ptr = camX;
          *radian_y_ptr = camY;
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
        *base_address -
        supported_builds[game_build_].auto_center_strength_offset;

    if (supported_builds[game_build_].cam_type_address != NULL) {
      uint8_t cam_type = GetCamType();

      if (cam_type &&
          (cam_type == 10 || cam_type == 13)) {  // Carriage / mine cart
        // Adjust addresses for carriage / mine cart
        uint32_t carriage_x_address = x_address - 0x810;
        uint32_t carriage_y_address = y_address - 0x810;
        uint32_t carriage_z_address = z_address - 0x810;
        uint32_t auto_center_strength_address_carriage =
            carriage_x_address + 0x74;

        // Perform sanity check
        uint32_t check_address = carriage_x_address + 0x78;
        xe::be<uint32_t>* check_value_ptr =
            kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(check_address);

        if (check_value_ptr && *check_value_ptr != 0xCDCDCDCD) {
          // Sanity check failed, use cached addresses if available
          if (cached_carriage_x_address != 0) {
            carriage_x_address = cached_carriage_x_address;
            carriage_y_address = cached_carriage_y_address;
            carriage_z_address = cached_carriage_z_address;
            auto_center_strength_address_carriage =
                cached_auto_center_strength_address_carriage;
          } else {
            // Cached addresses not available, cannot proceed
            return false;
          }
        } else {
          // Sanity check passed, cache the addresses
          cached_carriage_x_address = carriage_x_address;
          cached_carriage_y_address = carriage_y_address;
          cached_carriage_z_address = carriage_z_address;
          cached_auto_center_strength_address_carriage =
              auto_center_strength_address_carriage;
        }

        // Update x_address, y_address, z_address to the carriage addresses
        xe::be<float>* cam_x_carriage =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(
                carriage_x_address);

        xe::be<float>* cam_y_carriage =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(
                carriage_y_address);

        xe::be<float>* cam_z_carriage =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(
                carriage_z_address);

        xe::be<float>* auto_center_strength_carriage =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(
                auto_center_strength_address_carriage);

        // Calculate the horizontal and vertical angles
        float hor_angle = atan2(*cam_z_carriage, *cam_x_carriage);
        float vert_angle = asin(*cam_y_carriage);
        hor_angle -=
            invert_x_multiplier *
            ((input_state.mouse.x_delta * (float)cvars::sensitivity) / divisor);
        vert_angle = ClampVerticalAngle(
            vert_angle +
            (invert_y_multiplier *
             ((input_state.mouse.y_delta * (float)cvars::sensitivity)) /
             divisor));

        // Calculate 3D camera vector |
        // https://github.com/isJuhn/KAMI/blob/master/KAMI.Core/Cameras/HVVecCamera.cs
        *cam_x_carriage = cos(hor_angle) * cos(vert_angle);
        *cam_z_carriage = sin(hor_angle) * cos(vert_angle);
        *cam_y_carriage = sin(vert_angle);

        float auto_center_strength_c_f = *auto_center_strength_carriage;

        if (supported_builds[game_build_].auto_center_strength_offset != NULL &&
            auto_center_strength_c_f <= 1.f &&
            (input_state.mouse.x_delta != 0 ||
             input_state.mouse.y_delta != 0)) {
          auto_center_strength_c_f +=
              0.15f;  // Maybe setting it to 1.f is better, I'm
                      // just hoping += 0.x makes it smoother..
          if (auto_center_strength_c_f > 1.f) {
            auto_center_strength_c_f = 1.f;
          }
          *auto_center_strength_carriage = auto_center_strength_c_f;
        }
      }
    }

    xe::be<float>* cam_x_act =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(x_address);

    xe::be<float>* cam_y_act =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(y_address);

    xe::be<float>* cam_z_act =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(z_address);

    xe::be<float>* auto_center_strength_act =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(
            auto_center_strength_address);

    float auto_center_strength = *auto_center_strength_act;

    float cam_x = *cam_x_act;
    float cam_y = *cam_y_act;
    float cam_z = *cam_z_act;

    // Calculate the horizontal and vertical angles
    float hor_angle = atan2(cam_z, cam_x);
    float vert_angle = asin(cam_y);

    hor_angle -=
        invert_x_multiplier *
        ((input_state.mouse.x_delta * (float)cvars::sensitivity) / divisor);
    vert_angle = ClampVerticalAngle(
        vert_angle +
        (invert_y_multiplier *
         ((input_state.mouse.y_delta * (float)cvars::sensitivity)) / divisor));

    // Calculate 3D camera vector |
    // https://github.com/isJuhn/KAMI/blob/master/KAMI.Core/Cameras/HVVecCamera.cs
    cam_x = cos(hor_angle) * cos(vert_angle);
    cam_z = sin(hor_angle) * cos(vert_angle);
    cam_y = sin(vert_angle);

    /* if (cam_y > 0.7153550260f)
      cam_y = 0.7153550260f;
    else if (cam_y < -0.861205390f)
      cam_y = -0.861205390f;
      */
    if (IsWeaponWheelShown()) {
      HandleWeaponWheelEmulation(input_state, out_state);
    } else {
      *cam_x_act = cam_x;
      *cam_y_act = cam_y;
      *cam_z_act = cam_z;
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
    if (supported_builds[game_build_].mounting_center_address != NULL) {
      xe::be<uint32_t>* mounting_center_pointer =
          kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              supported_builds[game_build_].mounting_center_address);
      xe::be<uint32_t> mounting_center_final =
          *mounting_center_pointer + 0x1F00;

      auto* mounting_sanity =
          kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              mounting_center_final + 0x40);
      static uint32_t shoul = 0x73686F75;
      auto* mounting_center =
          kernel_memory()->TranslateVirtual<uint8_t*>(mounting_center_final);

      if (*mounting_center != 0 && *mounting_sanity == shoul &&
          IsMouseMoving(input_state)) {
        *mounting_center = 0;
      } else if (cached_mounting_center_final != 0) {
        // Use cached address if sanity check fails
        mounting_center_final = cached_mounting_center_final;
        mounting_center =
            kernel_memory()->TranslateVirtual<uint8_t*>(mounting_center_final);
        if (*mounting_center != 0 && IsMouseMoving(input_state)) {
          *mounting_center = 0;
        }
      }
    }
  } else
    HandleRightStickEmulation(input_state, out_state);
  return true;
}
bool RedDeadRedemptionGame::IsWeaponWheelShown() {
  if (supported_builds[game_build_].weapon_wheel_base_address != NULL) {
    xe::be<uint32_t>* weapon_wheel_result =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].weapon_wheel_base_address);
    xe::be<uint32_t> weapon_wheel_read =
        *weapon_wheel_result +
        supported_builds[game_build_].weapon_wheel_offset;
    auto* weapon_wheel_status =
        kernel_memory()->TranslateVirtual<uint8_t*>(weapon_wheel_read);
    if (*weapon_wheel_status == 2)
      return true;
    else
      return false;

  } else
    return false;
}
void RedDeadRedemptionGame::HandleWeaponWheelEmulation(
    RawInputState& input_state, X_INPUT_STATE* out_state) {
  if (IsMouseMoving(input_state)) {
    if (cvars::rdr_snappy_wheel) {
      static float xn = 0.0f;
      static float yn = 0.0f;

      float mouse_delta_x = input_state.mouse.x_delta / 2.5f;
      float mouse_delta_y = input_state.mouse.y_delta / 2.5f;

      xn += mouse_delta_x;
      yn += mouse_delta_y;

      if (xn > 1.0f) xn = 1.0f;
      if (xn < -1.0f) xn = -1.0f;
      if (yn > 1.0f) yn = 1.0f;
      if (yn < -1.0f) yn = -1.0f;

      float angle = atan2(yn, xn);
      float angle_degrees = RadianstoDegree(angle);

      if (angle_degrees < 0) {
        angle_degrees += 360.0f;
      }
      float dominance_threshold = 0.45f;

      if (fabs(xn) > fabs(yn) + dominance_threshold) {
        angle_degrees = (xn > 0) ? 0.0f : 180.0f;
      } else if (fabs(yn) > fabs(xn) + dominance_threshold) {
        angle_degrees = (yn > 0) ? 90.0f : 270.0f;
      } else {
        float segment_size = 45.0f;
        angle_degrees = roundf(angle_degrees / segment_size) * segment_size;
      }

      float snapped_angle_radians = DegreetoRadians(angle_degrees);

      xn = cosf(snapped_angle_radians);
      yn = sinf(snapped_angle_radians);

      out_state->gamepad.thumb_rx = static_cast<short>(xn * SHRT_MAX);
      out_state->gamepad.thumb_ry =
          static_cast<short>(-yn * SHRT_MAX);  // Invert Y-axis
    } else {
      static float xn = 0.0f;
      static float yn = 0.0f;

      xn += input_state.mouse.x_delta / 50.f;
      yn += input_state.mouse.y_delta / 50.f;
      if (xn > 1.0f) xn = 1.0f;
      if (xn < -1.0f) xn = -1.0f;
      if (yn > 1.0f) yn = 1.0f;
      if (yn < -1.0f) yn = -1.0f;
      out_state->gamepad.thumb_rx = static_cast<short>(xn * SHRT_MAX);
      out_state->gamepad.thumb_ry =
          static_cast<short>(-yn * SHRT_MAX);  // Invert Y-axis
    }
  }
}
bool RedDeadRedemptionGame::IsCinematicTypeEnabled() {
  if (supported_builds[game_build_].cinematicCam_address != NULL) {
    uint8_t cam_type = GetCamType();
    if (cam_type == 2) {
      return false;
    }

    uint8_t* cinematic_type_ptr = kernel_memory()->TranslateVirtual<uint8_t*>(
        supported_builds[game_build_].cinematicCam_address);
    if (*cinematic_type_ptr == 131) {
      return true;
    }
  }
  return false;
}
bool RedDeadRedemptionGame::IsPaused() {
  if (supported_builds[game_build_].pause_flag_address != NULL) {
    xe::be<uint8_t>* isPaused =
        kernel_memory()->TranslateVirtual<xe::be<uint8_t>*>(
            supported_builds[game_build_].pause_flag_address);
    if (isPaused && *isPaused >= 4) {
      return true;
    } else
      return false;
  } else
    return false;
}
void RedDeadRedemptionGame::HandleRightStickEmulation(
    RawInputState& input_state, X_INPUT_STATE* out_state) {
  auto now = std::chrono::steady_clock::now();
  auto elapsed_x = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_movement_time_x_)
                       .count();
  auto elapsed_y = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - last_movement_time_y_)
                       .count();

  static float accumulated_x = 0.0f;
  static float accumulated_y = 0.0f;

  const long long hold_time =
      static_cast<long long>(cvars::right_stick_hold_time_workaround);

  if (input_state.mouse.x_delta != 0) {
    float delta_x =
        (input_state.mouse.x_delta * 50.f) * (float)cvars::sensitivity;
    accumulated_x += delta_x;
    accumulated_x = std::clamp(accumulated_x, (float)SHRT_MIN, (float)SHRT_MAX);
    last_movement_time_x_ = now;
  } else if (elapsed_x < hold_time) {  // Hold the last accumulated value
    accumulated_x = std::clamp(accumulated_x, (float)SHRT_MIN, (float)SHRT_MAX);
  } else {
    accumulated_x = 0.0f;
  }

  if (input_state.mouse.y_delta != 0) {
    float delta_y =
        (input_state.mouse.y_delta * 50.f) * (float)cvars::sensitivity;

    accumulated_y -= delta_y;
    accumulated_y = std::clamp(accumulated_y, (float)SHRT_MIN, (float)SHRT_MAX);
    last_movement_time_y_ = now;
  } else if (elapsed_y < hold_time) {  // Hold the last accumulated value
    accumulated_y = std::clamp(accumulated_y, (float)SHRT_MIN, (float)SHRT_MAX);
  } else {
    accumulated_y = 0.0f;
  }

  out_state->gamepad.thumb_rx = static_cast<short>(accumulated_x);
  out_state->gamepad.thumb_ry = static_cast<short>(accumulated_y);
}

float RedDeadRedemptionGame::ClampVerticalAngle(float cam_y) {
  const float max_y_angle = 0.8f;
  const float min_y_angle = -1.1f;

  return std::clamp(cam_y, min_y_angle, max_y_angle);
}

static uint32_t cached_cam_type_address = 0;

uint8_t RedDeadRedemptionGame::GetCamType() {
  if (cached_cam_type_address != 0) {
    uint8_t* cam_type_ptr =
        kernel_memory()->TranslateVirtual<uint8_t*>(cached_cam_type_address);
    if (cam_type_ptr) {
      return *cam_type_ptr;  // Return the cached camera type
    }
    return 0;
  }

  // Otherwise, we need to search for the pattern in memory
  uint32_t start_address = 0xBA000A00;  // camera stuff start
  uint32_t end_address = 0xBF000000;

  // Pattern you are searching for with wildcards (assuming 0xCC is the
  // wildcard)
  std::vector<uint8_t> pattern = {
      0xCC, 0xCC, 0xCC, 0xCC,  // Wildcards for the actual address
      0x00, 0x0F, 0x00, 0x10,  // Specific bytes in the pattern
      0x00, 0x00, 0x3F, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};

  // Find the pattern in console memory (one-time scan)
  cached_cam_type_address = FindPatternWithWildcardAddress(
      start_address, end_address, pattern, "cam_type");

  // If we found the address, translate it and return the camera type
  if (cached_cam_type_address != 0) {
    uint8_t* cam_type_ptr =
        kernel_memory()->TranslateVirtual<uint8_t*>(cached_cam_type_address);
    if (cam_type_ptr) {
      return *cam_type_ptr;  // Return the camera type from the found address
    }
  }

  return 0;  // Default to 0 if no pattern was found or the cam_type_ptr is
             // invalid
}

uint32_t RedDeadRedemptionGame::FindPatternWithWildcardAddress(
    uint32_t start_address, uint32_t end_address,
    const std::vector<uint8_t>& pattern, const char* pattern_name) {
  // Translate the start and end addresses
  auto* memory_base =
      kernel_memory()->TranslateVirtual<uint8_t*>(start_address);
  auto* memory_end = kernel_memory()->TranslateVirtual<uint8_t*>(end_address);

  size_t pattern_length = pattern.size();

  for (auto* current_address = memory_base; current_address < memory_end;
       current_address++) {
    // Debug: Print current memory address being checked
    // printf("Checking memory at address: %p\n", current_address);

    // Compare the memory with the pattern
    if (CompareMemoryWithPattern(current_address, pattern)) {
      uint32_t guest_virtual_address =
          start_address + (uint32_t)(current_address - memory_base);

      printf("Pattern '%s' found at address: 0x%08X\n", pattern_name,
             guest_virtual_address - 0x1);

      // Return the 32-bit guest virtual address
      return guest_virtual_address - 0x1;
    }
  }

  return 0;
}
bool RedDeadRedemptionGame::CompareMemoryWithPattern(
    const uint8_t* memory, const std::vector<uint8_t>& pattern) {
  for (size_t i = 0; i < pattern.size(); i++) {
    // If the pattern byte is 0xCC (wildcard), we skip checking
    if (pattern[i] == 0xCC) {
      continue;  // 0xCC acts as a wildcard and matches anything
    }
    // Compare the memory byte with the pattern byte
    // printf("Comparing memory[%zu] = %02X with pattern[%zu] = %02X\n", i,
    //    memory[i], i, pattern[i]);
    if (memory[i] != pattern[i]) {
      return false;  // Mismatch, so the pattern does not match here
    }
  }
  return true;  // Pattern matches
}

std::string RedDeadRedemptionGame::ChooseBinds() {
  if (!IsPaused()) {
    if (uint8_t player_status = GetCamType();
        player_status == 8 || player_status == 10) {
      return "Horse";
    } else
      return "Default";
  } else
    return "Default";
}

bool RedDeadRedemptionGame::IsMouseMoving(const RawInputState& input_state) {
  static auto last_movement_time = std::chrono::steady_clock::now();
  const long long movement_timeout_ms = 50;

  if (input_state.mouse.x_delta != 0 ||
      input_state.mouse.y_delta !=
          0) {  // this if statement if used alone is unreliable, causes missed
                // mouse inputs, need to hold the state of it thus the need for
                // this function.
    last_movement_time = std::chrono::steady_clock::now();
    return true;
  } else {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - last_movement_time)
                          .count();

    if (elapsed_ms < movement_timeout_ms) {
      return true;
    } else {
      return false;
    }
  }
}

bool RedDeadRedemptionGame::ModifierKeyHandler(uint32_t user_index,
                                               RawInputState& input_state,
                                               X_INPUT_STATE* out_state) {
  uint16_t buttons = out_state->gamepad.buttons;
  buttons |= 0x1000;
  uint8_t player_status = GetCamType();
  /*
  2 = Duel
  6 = Turret
  7 = Cannon
  8 = Horse
  9 = Cover
  10 = Coach
  13 = Minecart
  */
  if (!IsPaused() && IsCinematicTypeEnabled() &&
      (cvars::rdr_turbo_gallop_horse ||
       player_status != 8 && player_status != 10)) {
    static auto last_toggle_time = std::chrono::steady_clock::now();
    static bool a_button_pressed = false;

    auto now = std::chrono::steady_clock::now();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - last_toggle_time)
                          .count();

    const int spam_interval_ms = 100;

    if (elapsed_ms >= spam_interval_ms) {
      a_button_pressed = !a_button_pressed;
      last_toggle_time = now;
    }

    if (a_button_pressed) {
      out_state->gamepad.buttons = buttons;
    }
  } else
    out_state->gamepad.buttons = buttons;
  return true;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe