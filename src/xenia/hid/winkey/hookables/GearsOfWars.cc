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
DECLARE_double(fov_sensitivity);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);
DECLARE_double(right_stick_hold_time_workaround);
DECLARE_int32(ue3_use_timer_to_hook_workaround);
DECLARE_bool(use_right_stick_workaround);
DECLARE_bool(use_right_stick_workaround_gears1and2);

const uint32_t kTitleIdGearsOfWars3 = 0x4D5308AB;
const uint32_t kTitleIdGearsOfWars2 = 0x4D53082D;
const uint32_t kTitleIdGearsOfWars1 = 0x4D5307D5;
const uint32_t kTitleIdGearsOfWarsJudgment = 0x4D530A26;
const uint32_t kTitleIdSection8 = 0x475007D4;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  uint32_t check_addr;
  uint32_t check_value;
  uint32_t title_id;
  uint32_t hook_moment_address;
  uint32_t hook_moment_address_alt;  // incase first one doesn't work
  uint32_t camera_base_address;
  uint32_t x_offset;
  uint32_t y_offset;
  uint32_t LookRightScale_address;
  uint32_t LookRightScale_live_address;
  uint32_t LookRightScale_live_offset_1;  // First offset, e.g., 0x6D4
  uint32_t LookRightScale_live_offset_2;  // Second offset, e.g., 0x154
  uint32_t fovscale_ptr_address;
  uint32_t fovscale_offset;
  uint16_t max_up;
  uint16_t max_down;
};

std::map<GearsOfWarsGame::GameBuild, GameBuildAddrs> supported_builds{
    {GearsOfWarsGame::GameBuild::GearsOfWars2_TU6,
     {0x8317A198, 0x47656172, kTitleIdGearsOfWars2, 0x830F6DF6, 0x8317016B,
      0x40874800, 0x66, 0x62, 0x404E8840, NULL, NULL, NULL, 0x40874800, 0x390,
      10000, 53530}},
    {GearsOfWarsGame::GameBuild::GearsOfWars2_TU0,
     {0x831574EA, 0x47656172, kTitleIdGearsOfWars2, 0x83105B23, 0x8312384F,
      0x408211C0, 0x66, 0x62, 0x405294C0, NULL, NULL, NULL, 0x408211C0, 0x390,
      10000, 53535}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU0,
     {0x834776EE, 0x47656172, kTitleIdGearsOfWars3, 0x833A480E, 0x83429A3E,
      0x43F6F340, 0x66, 0x62, 0x404E4054, NULL, NULL, NULL, 0x43F6F340, 0x3A8,
      10000, 53535}},
    {GearsOfWarsGame::GameBuild::GearsOfWars3_TU6,
     {0x8348848A, 0x47656172, kTitleIdGearsOfWars3, 0x833B4FCE, 0x830042CF,
      0x42145D40, 0x66, 0x62, 0x40502254, NULL, NULL, NULL, 0x42145D40, 0x3A8,
      10000, 53535}},
    {GearsOfWarsGame::GameBuild::GearsOfWarsJudgment_TU0,
     {0x8358ABEA, 0x47656172, kTitleIdGearsOfWarsJudgment, 0x83551871,
      0x83552939, 0x448F2840, 0x66, 0x62, 0x41DE7054, 0x448F2840, 0x6D4, 0x154,
      0x448F2840, 0x3AC, 10000, 53535}},
    {GearsOfWarsGame::GameBuild::GearsOfWarsJudgment_TU4,
     {0x8359C4AE, 0x47656172, kTitleIdGearsOfWarsJudgment, 0x8356C392,
      0x8356C392, 0x42943440, 0x66, 0x62, 0x41F2F754, 0x42943440, 0x6D4, 0x154,
      0x42943440, 0x3AC, 10000, 53535}},
    {GearsOfWarsGame::GameBuild::GearsOfWars1_TU0,
     {0x82C20CFA, 0x47656172, kTitleIdGearsOfWars1, 0x82BBDD87, 0x82BD28A3,
      0x49EAC460, 0xDE, 0xDA, 0x40BF0164, NULL, NULL, NULL, 0x426AD3CC, 0x2D4,
      10000, 53535}},
    {GearsOfWarsGame::GameBuild::GearsOfWars1_TU5,
     {0x8300235A, 0x47656172, kTitleIdGearsOfWars1, 0x82F9E99B, 0x82FDB677,
      0x4A1CBA60, 0xDE, 0xDA, 0x40BF9814, NULL, NULL, NULL, 0x42961700, 0x2D4,
      10000, 53535}},
    {GearsOfWarsGame::GameBuild::Section8_TU0,
     {0x8323DCCF, 0x656E6769, kTitleIdSection8, 0x8326F1AF, 0x8326F1B3,
      0x42231700, 0x66, 0x62, NULL, NULL, NULL, NULL, 0x42231700, 0x470, 16383,
      49152}}};

GearsOfWarsGame::~GearsOfWarsGame() = default;
static bool bypass_conditions = false;
bool GearsOfWarsGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdGearsOfWars3 &&
      kernel_state()->title_id() != kTitleIdGearsOfWars2 &&
      kernel_state()->title_id() != kTitleIdGearsOfWars1 &&
      kernel_state()->title_id() != kTitleIdGearsOfWarsJudgment &&
      kernel_state()->title_id() != kTitleIdSection8) {
    return false;
  }
  uint32_t title_id = kernel_state()->title_id();
  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    if (build.second.title_id != title_id) {  // Required check otherwise GOW1
                                              // crashes due to invalid address
      continue;
    }

    auto* build_ptr = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
        build.second.check_addr);

    if (build_ptr == nullptr) {
      continue;
    }

    if (*build_ptr == build.second.check_value) {
      game_build_ = build.first;
      static auto start_time = std::chrono::steady_clock::now();
      if ((cvars::ue3_use_timer_to_hook_workaround > 0) && !bypass_conditions) {
        if (!bypass_conditions) {
          auto current_time = std::chrono::steady_clock::now();
          auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(
              current_time - start_time);
          if (elapsed_time.count() >= cvars::ue3_use_timer_to_hook_workaround) {
            bypass_conditions = true;
          }
        }
      }
      if ((cvars::ue3_use_timer_to_hook_workaround <= 0) &&
          !bypass_conditions) {
        auto* hook_moment = kernel_memory()->TranslateVirtual<uint8_t*>(
            supported_builds[game_build_].hook_moment_address);
        auto* hook_moment_alt = kernel_memory()->TranslateVirtual<uint8_t*>(
            supported_builds[game_build_].hook_moment_address_alt);

        if (*hook_moment != 0 || *hook_moment_alt != 0) {
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
        if (bypass_conditions &&
            supported_builds[game_build_].LookRightScale_live_address) {
          uint32_t live_base_address = ResolveMultiPointer(
              supported_builds[game_build_].LookRightScale_live_address,
              supported_builds[game_build_].LookRightScale_live_offset_1,
              supported_builds[game_build_].LookRightScale_live_offset_2);

          if (live_base_address) {
            xe::be<float>* LookRightScale_live =
                kernel_memory()->TranslateVirtual<xe::be<float>*>(
                    live_base_address);
            xe::be<float>* LookUpScale_live =
                kernel_memory()->TranslateVirtual<xe::be<float>*>(
                    live_base_address + 0x4);

            if (LookRightScale_live && *LookRightScale_live != 0.05f) {
              *LookRightScale_live = 0.05f;
            }
            if (LookUpScale_live && *LookUpScale_live != 0.05f) {
              *LookUpScale_live = 0.05f;
            }
          }
        }
      }

      return true;
    }
  }

  return false;
}

bool GearsOfWarsGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                              X_INPUT_STATE* out_state) {
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

  if (bypass_conditions) {
    xe::be<uint16_t>* degree_x;
    xe::be<uint16_t>* degree_y;
    // printf("Current Build: %d\n", static_cast<int>(game_build_));
    uint32_t base_address =
        *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].camera_base_address);
    // printf("BASE ADDRESS: 0x%08X\n", base_address);
    if (base_address && base_address >= 0x40000000 &&
        base_address < 0x50000000) {
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
      float divisor = 10.f * FOVScale();
      uint16_t x_delta = static_cast<uint16_t>(
          (input_state.mouse.x_delta * divisor) * cvars::sensitivity);
      uint16_t y_delta = static_cast<uint16_t>(
          (input_state.mouse.y_delta * divisor) * cvars::sensitivity);
      if (!cvars::invert_x) {
        *degree_x += x_delta;
      } else {
        *degree_x -= x_delta;
      }
      uint16_t degree_y_calc = *degree_y;
      if (!cvars::invert_y) {
        degree_y_calc -= y_delta;
      } else {
        degree_y_calc += y_delta;
      }
      if (supported_builds[game_build_].max_up)
        ClampYAxis(degree_y_calc, supported_builds[game_build_].max_down,
                   supported_builds[game_build_].max_up);
      *degree_y = degree_y_calc;
    } else {
      return false;
    }
  }
  return true;
}

float GearsOfWarsGame::FOVScale() {
  if (supported_builds[game_build_].fovscale_ptr_address) {
    uint32_t fovscale_address =
        *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].fovscale_ptr_address);
    if (fovscale_address && fovscale_address >= 0x40000000 &&
        fovscale_address < 0x50000000) {
      float fovscale = *kernel_memory()->TranslateVirtual<xe::be<float>*>(
          fovscale_address + supported_builds[game_build_].fovscale_offset);
      float calc_fovscale = fovscale;
      if (calc_fovscale <= 0.f || calc_fovscale > 1.0f) {
        return 1.0f;
      }
      const float a =
          (float)cvars::fov_sensitivity;  // Quadratic scaling to make
                                          // fovscale effect sens stronger
      if (calc_fovscale != 1.f) {
        calc_fovscale =
            (1 - a) * (calc_fovscale * calc_fovscale) + a * calc_fovscale;
        return calc_fovscale;
      }
      return calc_fovscale;
    }
  }
  return 1.0f;
}

void GearsOfWarsGame::ClampYAxis(uint16_t& value, uint16_t max_down,
                                 uint16_t max_up) {
  uint16_t upper_limit = (max_up + 5500) % 65536;
  if (max_up < upper_limit) {
    if (value >= max_up + 1 && value <= upper_limit) {
      value = max_up;
    }
  } else {
    if (value >= max_up + 1 || value <= upper_limit) {
      value = max_up;
    }
  }

  uint16_t lower_limit = (max_down + 65536 - 5500) % 65536;
  if (lower_limit < max_down) {
    if (value >= lower_limit && value <= max_down - 1) {
      value = max_down;
    }
  } else {
    if (value >= lower_limit || value <= max_down - 1) {
      value = max_down;
    }
  }
}

uint32_t GearsOfWarsGame::ResolveMultiPointer(uint32_t base_address,
                                              uint32_t offset_1,
                                              uint32_t offset_2) {
  uint32_t live_base_address = base_address;

  // Apply the first offset
  if (!(live_base_address && live_base_address >= 0x40000000 &&
        live_base_address < 0x80000000)) {
    return 0;
  }

  auto* next_value_ptr =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(live_base_address);
  if (!next_value_ptr) {
    return 0;
  }

  live_base_address = *next_value_ptr + offset_1;

  // Apply the second offset
  if (!(live_base_address && live_base_address >= 0x40000000 &&
        live_base_address < 0x80000000)) {
    return 0;
  }

  next_value_ptr =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(live_base_address);
  if (!next_value_ptr) {
    return 0;
  }

  live_base_address = *next_value_ptr + offset_2;

  // Final check for the resolved address
  if (!(live_base_address && live_base_address >= 0x40000000 &&
        live_base_address < 0x80000000)) {
    return 0;
  }

  return live_base_address;
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