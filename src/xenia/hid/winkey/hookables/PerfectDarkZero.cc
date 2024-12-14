/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/PerfectDarkZero.h"

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
DECLARE_double(right_stick_hold_time_workaround);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);
DECLARE_bool(ge_gun_sway);

const uint32_t kTitleIdPerfectDarkZero = 0x4D5307D3;

namespace xe {
namespace hid {
namespace winkey {
bool __inline IsKeyDown(uint8_t key) {
  return (GetAsyncKeyState(key) & 0x8000) == 0x8000;
}
struct GameBuildAddrs {
  const char* build_string;
  uint32_t build_string_addr;
  uint32_t base_address;
  uint32_t base_address_multi;
  uint32_t base_address_multi_offset;
  uint32_t cover_flag_offset;
  uint32_t x_offset;
  uint32_t y_offset;
  uint32_t turret_flag_offset;
  uint32_t turret_base_offset;
  uint32_t turret_x_offset;
  uint32_t turret_y_offset;
  uint32_t cover_x_offset;
  uint32_t hovercraft_base_offset;
  uint32_t hovercraft_y_offset;
  uint32_t gun_y_offset;  // These in-game are tied to camera, we decouple them
                          // with a patch.
  uint32_t gun_x_offset;
  uint32_t fovscale_address;
  uint32_t fovscale_alt_static_address;  // TODO: Check sanity of ptr fovscale
                                         // address, if incorrect use static
                                         // address instead
  uint32_t current_set_fov;  // Incase FOV is increased with a patch.
  uint32_t pause_offset;
};

std::map<PerfectDarkZeroGame::GameBuild, GameBuildAddrs> supported_builds{
    {PerfectDarkZeroGame::GameBuild::PerfectDarkZero_TU0,
     {"09.11.05.0052", 0x820CED70, 0x82D2AD38, 0x82E35468, 0x3B8, 0x16B9,
      0x150,           0x1674,     0x16AB,     0x5C,       0x3A0, 0x39C,
      0x1670,          0x5C,       0xE54,      0xF9C,      0xFA0, 0x82D68320,
      0x82E1B930,      0x820EC228, 0x16A3}},
    {PerfectDarkZeroGame::GameBuild::PerfectDarkZero_TU3,
     {"19.09.06.0082", 0x820CD9E0, 0x82E3C3E8, 0x82E34224, 0x3C4, 0x16B9,
      0x150,           0x1674,     0x16AB,     0x5C,       0x3A0, 0x39C,
      0x1670,          0x5C,       0xE54,      0xF9C,      0xFA0, 0x82D69048,
      0x82D3EED0,      0x820EAF40, 0x16A3}},
    {PerfectDarkZeroGame::GameBuild::PerfectDarkZero_PlatinumHitsTU15,
     {"12.09.06.0081", 0x820CD9C0, 0x82E3C3E8, 0x82E3622C, 0x3C4,
      0x16B9,          0x150,      0x1674,     0x16AB,     0x5C,
      0x3A0,           0x39C,      0x1670,     0x5C,       0xE54,
      0xF9C,           0xFA0,      0x82D69048, NULL,       0x820EAF20,
      0x16A3}}};

PerfectDarkZeroGame::~PerfectDarkZeroGame() = default;

bool PerfectDarkZeroGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdPerfectDarkZero) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    auto* build_ptr = kernel_memory()->TranslateVirtual<const char*>(
        build.second.build_string_addr);

    if (strcmp(build_ptr, build.second.build_string) == 0) {
      game_build_ = build.first;
      return true;
    }
  }

  return false;
}

float PerfectDarkZeroGame::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float PerfectDarkZeroGame::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}

bool PerfectDarkZeroGame::DoHooks(uint32_t user_index,
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

  xe::be<uint32_t>* base_address =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
          supported_builds[game_build_].base_address);
  if (supported_builds[game_build_].base_address_multi != NULL) {
    // Other candidates for this address on TU3 are.
    //  + 3C4 at 82E2F568,82E2FB30 & 82E34224( we are using this), the offset
    //  seems to be different in TU0?
    //  + 0x8 or +0x10 at 82D69048
    // This multi pointer actually holds the LOCAL PLAYER rather than Joanna!
    // base_address works fine in Missions / Combat Arena but will break for
    // player2 on coop, player2 will attempt to control Joanna rather than their
    // own player.

    // Maybe implement sanity check and if fails use our base_address?
    xe::be<uint32_t>* base_address_multi =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].base_address_multi);
    if (*base_address_multi != NULL) {
      base_address = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
          *base_address_multi +
          supported_builds[game_build_].base_address_multi_offset);
    }
  }

  xe::be<uint32_t>* radians_x_base =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(*base_address +
                                                           0xE4);

  if ((!base_address || *base_address == NULL) ||
      (!radians_x_base || *radians_x_base == NULL)) {
    // Not in game
    return false;
  }
  enum universal_addr_cam {
    HOVERCRAFT = 2,
    COVER = 3,
    JETPAC = 4,
    TURRET = 6,
  };

  bool in_jetpac = isSpecialCam(base_address, NULL, true, JETPAC);
  if (!IsPaused(base_address) || in_jetpac) {
    xe::be<uint32_t> x_address;
    xe::be<uint32_t> y_address;

    bool in_cover = isSpecialCam(
        base_address, supported_builds[game_build_].cover_flag_offset, true, 3);
    bool in_hovercraft = isSpecialCam(base_address, NULL, true, HOVERCRAFT);
    bool in_turret = false;
    bool in_turret2 = false;
    if (supported_builds[game_build_].turret_flag_offset) {
      in_turret = isSpecialCam(base_address,
                               supported_builds[game_build_].turret_flag_offset,
                               true, 6);
    }
    if (!in_cover) {
      x_address = *radians_x_base + supported_builds[game_build_].x_offset;
    } else {
      x_address = *base_address + supported_builds[game_build_].cover_x_offset;
    }
    y_address = *base_address + supported_builds[game_build_].y_offset;
    xe::be<uint32_t>* turret_base = NULL;
    if (in_turret) {
      xe::be<uint32_t>* turret_base =
          kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              *base_address + supported_builds[game_build_].turret_base_offset);

      if (*turret_base != NULL) {
        // MOUSEHOOK TODO: use static turret base address instead, currently I
        // use one the chains from the base address we have but it's points to
        // something else for a frame or two thus the address check, this makes
        // it easier to just copy and paste to other versions.

        if (*turret_base && *turret_base >= 0x00100000 &&
            *turret_base < 0x20000000) {
          in_turret2 = true;
          x_address =
              *turret_base + supported_builds[game_build_].turret_x_offset;
          y_address =
              *turret_base + supported_builds[game_build_].turret_y_offset;
        }
      }
    } else if (in_hovercraft || in_jetpac) {
      xe::be<uint32_t>* hovercraft_base =
          kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              *base_address +
              supported_builds[game_build_].hovercraft_base_offset);
      x_address = *hovercraft_base +
                  supported_builds[game_build_].hovercraft_y_offset + 0x4;
      y_address =
          *hovercraft_base + supported_builds[game_build_].hovercraft_y_offset;
    }

    xe::be<float>* cam_x =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(x_address);

    xe::be<float>* cam_y =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(y_address);

    float degree_x, degree_y;

    if (!in_cover || (in_turret2) || in_hovercraft || in_jetpac) {
      // Normal mode: convert radians to degrees
      degree_x = RadianstoDegree(*cam_x);
    } else {
      // Cover mode: X-axis is already in degrees
      degree_x = *cam_x;
    }
    if (in_turret2 || in_hovercraft || in_jetpac) {
      degree_y = RadianstoDegree(*cam_y);
    } else {
      degree_y = (float)*cam_y;
    }

    float set_fov_multiplier = 1.0f;
    static float fovscale_l = 1.0f;
    xe::be<uint32_t>* base_address_fov =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].fovscale_address);
    if (!base_address_fov || *base_address_fov != NULL) {
      xe::be<uint32_t> fovscale_address = *base_address_fov + 0x440;
      xe::be<uint32_t> fovscale_sanity = *base_address_fov + 0x660;

      xe::be<float>* set_fov =
          kernel_memory()->TranslateVirtual<xe::be<float>*>(
              supported_builds[game_build_].current_set_fov);

      xe::be<float>* fovscale =
          kernel_memory()->TranslateVirtual<xe::be<float>*>(fovscale_address);

      const float a = (float)cvars::fov_sensitivity;
      fovscale_l = *fovscale;
      if (*set_fov != 58.f) {
        set_fov_multiplier = *set_fov / 58.f;
      }

      fovscale_l = (*set_fov / *fovscale);

      if (fovscale_l >= 45.f) {
        fovscale_l /= 2.5;  // For snipers, otherwise it's too slow!
      }

      if (fovscale_l > 1.f) {
        fovscale_l =
            (a * fovscale_l + (1 - a) * (fovscale_l * fovscale_l) * 1.1f);
      }
    }

    // X-axis = 0 to 360
    if (cvars::invert_x) {
      degree_x += ((input_state.mouse.x_delta / set_fov_multiplier) /
                   (8.40517241378f * fovscale_l)) *
                  (float)cvars::sensitivity;
    } else {
      degree_x -= ((input_state.mouse.x_delta / set_fov_multiplier) /
                   (8.40517241378f * fovscale_l)) *
                  (float)cvars::sensitivity;
    }

    if (!in_cover || (in_turret2) || in_hovercraft || in_jetpac) {
      *cam_x = DegreetoRadians(
          degree_x);  // Convert degrees back to radians for normal aiming
    } else if (in_cover) {
      degree_x = std::clamp(degree_x, -68.0f, 68.0f);
      *cam_x = degree_x;  // Directly store degrees for cover aiming
    }

    // Y-axis = -90 to 90
    if (cvars::invert_y) {
      degree_y -= ((input_state.mouse.y_delta / set_fov_multiplier) /
                   (8.40517241378f * fovscale_l)) *
                  (float)cvars::sensitivity;
    } else {
      degree_y += ((input_state.mouse.y_delta / set_fov_multiplier) /
                   (8.40517241378f * fovscale_l)) *
                  (float)cvars::sensitivity;
    }
    if (in_turret2 || in_hovercraft || in_jetpac) {
      *cam_y = DegreetoRadians(degree_y);
    } else {
      *cam_y = degree_y;
    }
    if (cvars::ge_gun_sway) {
      xe::be<uint32_t> gun_x_address =
          *base_address + supported_builds[game_build_].gun_x_offset;
      xe::be<uint32_t> gun_y_address =
          *base_address + supported_builds[game_build_].gun_y_offset;

      // revised gun sway from goldeneye.cc
      xe::be<float>* gun_x =
          kernel_memory()->TranslateVirtual<xe::be<float>*>(gun_x_address);
      xe::be<float>* gun_y =
          kernel_memory()->TranslateVirtual<xe::be<float>*>(gun_y_address);

      float gun_x_val = *gun_x;
      float gun_y_val = *gun_y;

      // Apply the mouse input to the gun sway
      if (input_state.mouse.x_delta || input_state.mouse.y_delta) {
        if (!cvars::invert_x) {
          gun_x_val +=
              ((float)input_state.mouse.x_delta / (20.f * fovscale_l)) *
              (float)cvars::sensitivity;
        } else {
          gun_x_val -=
              ((float)input_state.mouse.x_delta / (20.f * fovscale_l)) *
              (float)cvars::sensitivity;
        }

        if (!cvars::invert_y) {
          gun_y_val +=
              ((float)input_state.mouse.y_delta / (20.f * fovscale_l)) *
              (float)cvars::sensitivity;
        } else {
          gun_y_val -=
              ((float)input_state.mouse.y_delta / (20.f * fovscale_l)) *
              (float)cvars::sensitivity;
        }

        // Bound the gun sway movement within a range to prevent excessive
        // movement
        float x_limit = 6.6f;
        float y_limit = 1.8f;
        xe::be<float>* gun_zoom =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(
                *base_address +
                0x1910);  // unneeded but we're using this as our aim check.
        if ((fovscale_l >= 1.05f) && *gun_zoom > 0.f) {
          x_limit /= (fovscale_l * 2.5f);
          y_limit /= (fovscale_l * 2.5f);
        }

        gun_x_val = std::min(gun_x_val, x_limit);
        gun_x_val = std::max(gun_x_val, -x_limit);
        gun_y_val = std::min(gun_y_val, y_limit);
        gun_y_val = std::max(gun_y_val, -y_limit);

        // Set centering and disable sway flags
        start_centering_ = true;
        disable_sway_ = true;  // Disable sway until centering is complete
      } else if (start_centering_) {
        // Apply gun centering if no input is detected
        float centering_speed =
            0.05f;  // Adjust the speed of centering as needed

        if (gun_x_val > 0) {
          gun_x_val -= std::min(centering_speed, gun_x_val);
        } else if (gun_x_val < 0) {
          gun_x_val += std::min(centering_speed, -gun_x_val);
        }

        if (gun_y_val > 0) {
          gun_y_val -= std::min(centering_speed, gun_y_val);
        } else if (gun_y_val < 0) {
          gun_y_val += std::min(centering_speed, -gun_y_val);
        }

        // Stop centering once the gun is centered
        if (gun_x_val == 0 && gun_y_val == 0) {
          start_centering_ = false;
          disable_sway_ = false;  // Re-enable sway after centering
        }
      }

      // Write the updated values back to the gun_x and gun_y
      *gun_x = gun_x_val;
      *gun_y = gun_y_val;
    }
  } else if (IsPaused(base_address) && !isSpecialCam(base_address, 0x16D1)) {
    HandleRightStickEmulation(input_state, out_state);
  } else if (IsPaused(base_address) && isSpecialCam(base_address, 0x16D1)) {
    HandleRightStickEmulation(input_state, out_state, true);
  }

  return true;
}

bool PerfectDarkZeroGame::IsPaused(xe::be<uint32_t>* player) {
  uint8_t* pause_flag = kernel_memory()->TranslateVirtual<uint8_t*>(
      *player + supported_builds[game_build_].pause_offset);
  if (*pause_flag != 0) {
    return true;
  } else {
    return false;
  }
}

bool PerfectDarkZeroGame::isSpecialCam(xe::be<uint32_t>* player,
                                       uint32_t special_cam_flag_offset,
                                       bool universal_addr, uint8_t cam_type) {
  if (!universal_addr) {
    uint8_t* special_cam_flag = kernel_memory()->TranslateVirtual<uint8_t*>(
        *player + special_cam_flag_offset);

    if (special_cam_flag && *special_cam_flag == 1) {
      return true;
    } else {
      return false;
    }
  } else {
    xe::be<uint32_t>* base_address =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].fovscale_address);

    if (!base_address || *base_address == 0) {
      return false;
    }

    xe::be<uint32_t>* ptr1 =
        kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(*base_address +
                                                             0x53C);
    if (!ptr1 || *ptr1 == 0) {
      return false;
    }

    uint8_t* current_cam =
        kernel_memory()->TranslateVirtual<uint8_t*>(*ptr1 + 0xB);
    if (!current_cam) {
      return false;
    }
    if (*current_cam == cam_type) {
      return true;
    } else {
      return false;
    }
  }
}

void PerfectDarkZeroGame::HandleRightStickEmulation(RawInputState& input_state,
                                                    X_INPUT_STATE* out_state,
                                                    bool LSmode) {
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
  } else if (!LSmode) {
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
  } else if (!LSmode) {
    accumulated_y = 0.0f;
  }
  if (!LSmode) {
    out_state->gamepad.thumb_rx = static_cast<short>(accumulated_x);
    out_state->gamepad.thumb_ry = static_cast<short>(accumulated_y);
  } else if (LSmode && !IsKeyDown(VK_SHIFT)) {
    out_state->gamepad.thumb_lx = static_cast<short>(accumulated_x);
    out_state->gamepad.thumb_ly = static_cast<short>(accumulated_y);
  }
}

std::string PerfectDarkZeroGame::ChooseBinds() { return "Default"; }

bool PerfectDarkZeroGame::ModifierKeyHandler(uint32_t user_index,
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
    distance /= 1.55f;

    out_state->gamepad.thumb_lx = (int16_t)(distance * cosf(angle));
    out_state->gamepad.thumb_ly = (int16_t)(distance * sinf(angle));
  }
  return true;
}
void PerfectDarkZeroGame::WeaponSwitchHandler(uint32_t user_index,
                                              RawInputState& input_state,
                                              X_INPUT_STATE* out_state,
                                              int weapon, uint16_t buttons) {}
}  // namespace winkey
}  // namespace hid
}  // namespace xe
