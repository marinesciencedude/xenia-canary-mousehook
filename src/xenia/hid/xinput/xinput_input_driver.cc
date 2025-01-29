/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/xinput/xinput_input_driver.h"

// Must be included before xinput.h to avoid windows.h conflicts:
#include "xenia/base/platform_win.h"

#include <xinput.h>  // NOLINT(build/include_order)

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/hid/hid_flags.h"

#include "xenia/hid/hookables/mousehook.h"

namespace xe {
namespace hid {
namespace xinput {

XInputInputDriver::XInputInputDriver(xe::ui::Window* window,
                                     size_t window_z_order)
    : InputDriver(window, window_z_order),
      window_input_listener_(*this),
      module_(nullptr),
      XInputGetCapabilities_(nullptr),
      XInputGetState_(nullptr),
      XInputGetStateEx_(nullptr),
      XInputGetKeystroke_(nullptr),
      XInputSetState_(nullptr),
      XInputEnable_(nullptr) {
  // Register our supported hookable games
  RegisterHookables(hookable_games_);

  // Read bindings file if it exists
  ParseCustomKeyBinding(key_binds_);

  window->AddInputListener(&window_input_listener_, window_z_order);
}

XInputInputDriver::~XInputInputDriver() {
  if (module_) {
    FreeLibrary((HMODULE)module_);
    module_ = nullptr;
    XInputGetCapabilities_ = nullptr;
    XInputGetState_ = nullptr;
    XInputGetStateEx_ = nullptr;
    XInputGetKeystroke_ = nullptr;
    XInputSetState_ = nullptr;
    XInputEnable_ = nullptr;
  }
}

X_STATUS XInputInputDriver::Setup() {
  HMODULE module = LoadLibraryW(L"xinput1_4.dll");
  if (!module) {
    return X_STATUS_DLL_NOT_FOUND;
  }

  // Support guide button with XInput using XInputGetStateEx
  // https://source.winehq.org/git/wine.git/?a=commit;h=de3591ca9803add117fbacb8abe9b335e2e44977
  auto const XInputGetStateEx = (LPCSTR)100;

  // Required.
  auto xigc = GetProcAddress(module, "XInputGetCapabilities");
  auto xigs = GetProcAddress(module, "XInputGetState");
  auto xigsEx = GetProcAddress(module, XInputGetStateEx);
  auto xigk = GetProcAddress(module, "XInputGetKeystroke");
  auto xiss = GetProcAddress(module, "XInputSetState");

  // Not required.
  auto xie = GetProcAddress(module, "XInputEnable");

  // Only fail when we don't have the bare essentials;
  if (!xigc || !xigs || !xigk || !xiss) {
    FreeLibrary(module);
    return X_STATUS_PROCEDURE_NOT_FOUND;
  }

  module_ = module;
  XInputGetCapabilities_ = xigc;
  XInputGetState_ = xigs;
  XInputGetStateEx_ = xigsEx;
  XInputGetKeystroke_ = xigk;
  XInputSetState_ = xiss;
  XInputEnable_ = xie;

  return X_STATUS_SUCCESS;
}

constexpr uint64_t SKIP_INVALID_CONTROLLER_TIME = 1100;
static uint64_t last_invalid_time[4];

static DWORD should_skip(uint32_t user_index) {
  uint64_t time = last_invalid_time[user_index];
  if (time) {
    uint64_t deltatime = xe::Clock::QueryHostUptimeMillis() - time;

    if (deltatime < SKIP_INVALID_CONTROLLER_TIME) {
      return ERROR_DEVICE_NOT_CONNECTED;
    }
    last_invalid_time[user_index] = 0;
  }
  return 0;
}

static void set_skip(uint32_t user_index) {
  last_invalid_time[user_index] = xe::Clock::QueryHostUptimeMillis();
}

X_RESULT XInputInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  DWORD skipper = should_skip(user_index);
  if (skipper) {
    return skipper;
  }
  XINPUT_CAPABILITIES native_caps;
  auto xigc = (decltype(&XInputGetCapabilities))XInputGetCapabilities_;
  DWORD result =
      xigc(user_index, flags & ~X_INPUT_DEVTYPE::XINPUT_DEVTYPE_KEYBOARD,
           &native_caps);
  if (result) {
    if (result == ERROR_DEVICE_NOT_CONNECTED) {
      set_skip(user_index);
    }
    return result;
  }

  out_caps->type = native_caps.Type;
  out_caps->sub_type = native_caps.SubType;
  out_caps->flags = native_caps.Flags;
  out_caps->gamepad.buttons = native_caps.Gamepad.wButtons;
  out_caps->gamepad.left_trigger = native_caps.Gamepad.bLeftTrigger;
  out_caps->gamepad.right_trigger = native_caps.Gamepad.bRightTrigger;
  out_caps->gamepad.thumb_lx = native_caps.Gamepad.sThumbLX;
  out_caps->gamepad.thumb_ly = native_caps.Gamepad.sThumbLY;
  out_caps->gamepad.thumb_rx = native_caps.Gamepad.sThumbRX;
  out_caps->gamepad.thumb_ry = native_caps.Gamepad.sThumbRY;
  out_caps->vibration.left_motor_speed = native_caps.Vibration.wLeftMotorSpeed;
  out_caps->vibration.right_motor_speed =
      native_caps.Vibration.wRightMotorSpeed;

  return result;
}

X_RESULT XInputInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  DWORD skipper = should_skip(user_index);
  if (skipper) {
    return skipper;
  }

  // Added padding in case we are using XInputGetStateEx
  struct {
    XINPUT_STATE state;
    unsigned int dwPaddingReserved;
  } native_state;

  // If the guide button is enabled use XInputGetStateEx, otherwise use the
  // default XInputGetState.
  auto xigs = cvars::guide_button ? (decltype(&XInputGetState))XInputGetStateEx_
                                  : (decltype(&XInputGetState))XInputGetState_;

  DWORD result = xigs(user_index, &native_state.state);
  if (result) {
    if (result == ERROR_DEVICE_NOT_CONNECTED) {
      set_skip(user_index);
    }
    return result;
  }

  uint16_t buttons = native_state.state.Gamepad.wButtons;
  uint8_t left_trigger = native_state.state.Gamepad.bLeftTrigger;
  uint8_t right_trigger = native_state.state.Gamepad.bRightTrigger;
  int16_t thumb_lx = native_state.state.Gamepad.sThumbLX;
  int16_t thumb_ly = native_state.state.Gamepad.sThumbLY;
  int16_t thumb_rx = native_state.state.Gamepad.sThumbRX;
  int16_t thumb_ry = native_state.state.Gamepad.sThumbRY;
  bool modifier_pressed = false;
  bool weapon_switch = false;
  int weapon = 0;

  RawInputState state;

  if (window()->HasFocus() && is_active()) {
    HandleKeyBindings(state, mouse_events_, key_states_, key_binds_, title_id,
                      hookable_games_, &buttons, &left_trigger, &right_trigger,
                      &thumb_lx, &thumb_ly, &thumb_rx, &thumb_ry,
                      &modifier_pressed, &weapon_switch, &weapon);
  } else {  // So keys don't get 'stuck' if they were held previously when
            // tabbing in and out from the window
    memset(key_states_, 0, 256);
    mouse_events_ = {};
  }

  out_state->packet_number = native_state.state.dwPacketNumber;
  if (is_active()) {
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = left_trigger;
    out_state->gamepad.right_trigger = right_trigger;
    out_state->gamepad.thumb_lx = thumb_lx;
    out_state->gamepad.thumb_ly = thumb_ly;
    out_state->gamepad.thumb_rx = thumb_rx;
    out_state->gamepad.thumb_ry = thumb_ry;
  } else {
    std::memset(&out_state->gamepad, 0, sizeof(out_state->gamepad));
  }

  // Check if we have any hooks/injections for the current game
  bool game_modifier_handled = false;
  if (title_id) {
    for (auto& game : hookable_games_) {
      if (game->IsGameSupported()) {
        game->DoHooks(user_index, state, out_state);
        if (modifier_pressed) {
          game_modifier_handled =
              game->ModifierKeyHandler(user_index, state, out_state);
        }
        if (weapon_switch) {
          game->WeaponSwitchHandler(user_index, state, out_state, weapon,
                                    buttons);
        }
        break;
      }
    }
  }

  return result;
}

X_RESULT XInputInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  DWORD skipper = should_skip(user_index);
  if (skipper) {
    return skipper;
  }
  XINPUT_VIBRATION native_vibration;
  native_vibration.wLeftMotorSpeed = vibration->left_motor_speed;
  native_vibration.wRightMotorSpeed = vibration->right_motor_speed;
  auto xiss = (decltype(&XInputSetState))XInputSetState_;
  DWORD result = xiss(user_index, &native_vibration);
  if (result == ERROR_DEVICE_NOT_CONNECTED) {
    set_skip(user_index);
  }
  return result;
}

X_RESULT XInputInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  // We may want to filter flags/user_index before sending to native.
  // flags is reserved on desktop.
  DWORD result;

  // XInputGetKeystroke on Windows has a bug where it will return
  // ERROR_SUCCESS (0) even if the device is not connected:
  // https://stackoverflow.com/questions/23669238/xinputgetkeystroke-returning-error-success-while-controller-is-unplugged
  //
  // So we first check if the device is connected via XInputGetCapabilities, so
  // we are not passing back an uninitialized X_INPUT_KEYSTROKE structure.
  // If any user (0xFF) is polled this bug does not occur but GetCapabilities
  // would fail so we need to skip it.
  if (user_index != XUserIndexAny) {
    XINPUT_CAPABILITIES caps;
    auto xigc = (decltype(&XInputGetCapabilities))XInputGetCapabilities_;
    result = xigc(user_index, 0, &caps);
    if (result) {
      return result;
    }
  }

  XINPUT_KEYSTROKE native_keystroke;
  auto xigk = (decltype(&XInputGetKeystroke))XInputGetKeystroke_;
  result = xigk(user_index, 0, &native_keystroke);
  if (result) {
    return result;
  }

  if (!is_active()) {
    return X_ERROR_EMPTY;
  }

  out_keystroke->virtual_key = native_keystroke.VirtualKey;
  out_keystroke->unicode = native_keystroke.Unicode;
  out_keystroke->flags = native_keystroke.Flags;
  out_keystroke->user_index = native_keystroke.UserIndex;
  out_keystroke->hid_code = native_keystroke.HidCode;
  // X_ERROR_EMPTY if no new keys
  // X_ERROR_DEVICE_NOT_CONNECTED if no device
  // X_ERROR_SUCCESS if key
  return result;
}

InputType XInputInputDriver::GetInputType() const {
  return InputType::Controller;
}

void XInputInputDriver::XInputWindowInputListener::OnRawMouse(
    ui::MouseEvent& e) {
  driver_.OnRawMouse(e);
}

void XInputInputDriver::OnRawMouse(ui::MouseEvent& evt) {
  if (!is_active()) {
    return;
  }

  OnMouse(evt, mouse_events_, key_states_);
}

}  // namespace xinput
}  // namespace hid
}  // namespace xe
