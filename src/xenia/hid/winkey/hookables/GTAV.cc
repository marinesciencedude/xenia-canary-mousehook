/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/GTAV.h"

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

const uint32_t kTitleIdGTAV = 0x545408A7;

namespace xe {
namespace hid {
namespace winkey {
// Don't really need checks or addresses when it's handled by the plugin.

GTAVGame::~GTAVGame() = default;

bool GTAVGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdGTAV) {
    return false;
  }

  return true;
}

bool GTAVGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                       X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  if (!GTAVMousehookHelperXenia_pluginloaded) {
    if (!kernel_state()->GetModule("GTAVMousehookHelperXenia.xex")) {
      return false;
    } else {
      GTAVMousehookHelperXenia_pluginloaded = true;
    }
  }

  if (mousehook_ShouldPullAroundWhenUsingMouse_addr == NULL ||
      plugin_delta_x_addr == NULL || plugin_delta_y_addr == NULL) {
    GetAddressesFromHelperPlugin(&mousehook_ShouldPullAroundWhenUsingMouse_addr,
                                 &plugin_delta_x_addr, &plugin_delta_y_addr);
  }

  if (mousehook_ShouldPullAroundWhenUsingMouse_addr && plugin_delta_x_addr &&
      plugin_delta_y_addr) {
    auto* mousehook_ShouldPullAroundWhenUsingMouse =
        kernel_memory()->TranslateVirtual<uint8_t*>(
            mousehook_ShouldPullAroundWhenUsingMouse_addr);

    xe::be<float>* plugin_delta_x =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(plugin_delta_x_addr);
    xe::be<float>* plugin_delta_y =
        kernel_memory()->TranslateVirtual<xe::be<float>*>(plugin_delta_y_addr);

    float delta_x = *plugin_delta_x;
    float delta_y = *plugin_delta_y;

    delta_x = (input_state.mouse.x_delta / 15.0f) * DTOR;
    delta_y = (input_state.mouse.y_delta / 15.0f) * DTOR;

    *plugin_delta_x = delta_x;
    *plugin_delta_y = delta_y;
    HandleMouseInput(mousehook_ShouldPullAroundWhenUsingMouse, delta_x,
                     delta_y);
    return true;
  }

  return true;
}

void GTAVGame::GetAddressesFromHelperPlugin(uint32_t* pullaround,
                                            uint32_t* delta_x,
                                            uint32_t* delta_y,
                                            uint32_t* player_status) {
  auto module = kernel_state()->GetModule("GTAVMousehookHelperXenia.xex");
  if (!module) {
    XELOGE("MOUSEHOOK: Failed to get module GTAVMousehookHelperXenia.xex");
    return;
  }

  if (pullaround && *pullaround == NULL) {
    *pullaround = module->GetProcAddressByOrdinal(3);
    XELOGI(fmt::format(
        "MOUSEHOOK: GTAVMousehookHelper plugin set pullaround to 0x{:X}",
        *pullaround));
  }

  if (delta_x && *delta_x == NULL) {
    *delta_x = module->GetProcAddressByOrdinal(1);
    XELOGI(fmt::format(
        "MOUSEHOOK: GTAVMousehookHelper plugin set delta_x to 0x{:X}",
        *delta_x));
  }

  if (delta_y && *delta_y == NULL) {
    *delta_y = module->GetProcAddressByOrdinal(2);
    XELOGI(fmt::format(
        "MOUSEHOOK: GTAVMousehookHelper plugin set delta_y to 0x{:X}",
        *delta_y));
  }

  if (player_status && *player_status == NULL) {
    *player_status = module->GetProcAddressByOrdinal(4);
    XELOGI(fmt::format(
        "MOUSEHOOK: GTAVMousehookHelper plugin set player_status to 0x{:X}",
        *player_status));
  }
}

void GTAVGame::HandleMouseInput(
    uint8_t* mousehook_ShouldPullAroundWhenUsingMouse, float delta_x,
    float delta_y) {
  auto now = std::chrono::steady_clock::now();

  if (delta_x != 0 || delta_y != 0) {
    *mousehook_ShouldPullAroundWhenUsingMouse = 1;
    last_mouse_input_time_ = now;
    is_mouse_input_active_ = true;
  }

  if (is_mouse_input_active_) {
    auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_mouse_input_time_);

    if (elapsed_time.count() >= 2430) {
      *mousehook_ShouldPullAroundWhenUsingMouse = 0;
      is_mouse_input_active_ = false;
    }
  }
}

std::string GTAVGame::ChooseBinds() {
  if (player_status_addr == NULL) {
    GetAddressesFromHelperPlugin(NULL, NULL, NULL, &player_status_addr);
    return "Default";
  }
  auto status =
      *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player_status_addr);
  switch (status) {
    case VEHICLE_TYPE_NONE:
    case VEHICLE_TYPE_NONE_AND_PAUSED:
      return "Default";
    case VEHICLE_TYPE_CAR:
    case VEHICLE_TYPE_BIKE:
    case VEHICLE_TYPE_BMX:
      return "Vehicle";
    case VEHICLE_TYPE_PLANE:
    case VEHICLE_TYPE_HELI:
      return "Aircraft";
    default:
      return "Vehicle";
  }
  return "Default";
}

bool GTAVGame::ModifierKeyHandler(uint32_t user_index,
                                  RawInputState& input_state,
                                  X_INPUT_STATE* out_state) {
  if (!player_status_addr) {
    return false;
  }

  auto status =
      *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(player_status_addr);
  uint16_t buttons = out_state->gamepad.buttons;  // Copy current button state

  static auto last_toggle_time = std::chrono::steady_clock::now();
  static bool a_button_pressed = false;
  auto now = std::chrono::steady_clock::now();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_toggle_time)
                        .count();

  const int spam_interval_ms = 33;

  if (status == VEHICLE_TYPE_NONE || status == VEHICLE_TYPE_BMX) {
    if (elapsed_ms >= spam_interval_ms) {
      a_button_pressed = !a_button_pressed;
      last_toggle_time = now;
    }

    if (a_button_pressed) {
      buttons |= 0x1000;
    } else {
      buttons &= ~0x1000;
    }
  } else {
    buttons |= 0x1000;
  }

  out_state->gamepad.buttons = buttons;
  return true;
}

void GTAVGame::WeaponSwitchHandler(uint32_t user_index,
                                   RawInputState& input_state,
                                   X_INPUT_STATE* out_state, int weapon,
                                   uint16_t buttons) {}

}  // namespace winkey
}  // namespace hid
}  // namespace xe