/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/winkey_input_driver.h"

#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/system.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"

#include "xenia/hid/hookables/mousehook.h"

#define XE_HID_WINKEY_BINDING(button, description, cvar_name, \
                              cvar_default_value)             \
  DEFINE_string(cvar_name, cvar_default_value,                \
                "List of keys to bind to " description        \
                ", separated by spaces",                      \
                "HID.WinKey")
#include "winkey_binding_table.inc"
#undef XE_HID_WINKEY_BINDING

DEFINE_int32(keyboard_mode, 1,
             "Allows user do specify keyboard working mode. Possible values: 0 "
             "- Disabled, 1 - Enabled, 2 - Passthrough. Passthrough requires "
             "controller being connected!",
             "HID");

DEFINE_int32(
    keyboard_user_index, 0,
    "Controller port that keyboard emulates. [0, 3] - Keyboard is assigned to "
    "selected slot. Passthrough does not require assigning slot.",
    "HID");

namespace xe {
namespace hid {
namespace winkey {

bool static IsPassthroughEnabled() {
  return static_cast<KeyboardMode>(cvars::keyboard_mode) ==
         KeyboardMode::Passthrough;
}

bool static IsKeyboardForUserEnabled(uint32_t user_index) {
  if (static_cast<KeyboardMode>(cvars::keyboard_mode) !=
      KeyboardMode::Enabled) {
    return false;
  }

  return cvars::keyboard_user_index == user_index;
}

bool __inline IsKeyToggled(uint8_t key) {
  return (GetKeyState(key) & 0x1) == 0x1;
}

bool __inline IsKeyDown(uint8_t key) {
  return (GetAsyncKeyState(key) & 0x8000) == 0x8000;
}

bool __inline IsKeyDown(ui::VirtualKey virtual_key) {
  return IsKeyDown(static_cast<uint8_t>(virtual_key));
}

void WinKeyInputDriver::ParseKeyBinding(ui::VirtualKey output_key,
                                        const std::string_view description,
                                        const std::string_view source_tokens) {
  for (const std::string_view source_token :
       utf8::split(source_tokens, " ", true)) {
    KeyBinding key_binding;
    key_binding.output_key = output_key;

    std::string_view token = source_token;

    if (utf8::starts_with(token, "_")) {
      key_binding.lowercase = true;
      token = token.substr(1);
    } else if (utf8::starts_with(token, "^")) {
      key_binding.uppercase = true;
      token = token.substr(1);
    }

    if (utf8::starts_with(token, "0x")) {
      token = token.substr(2);
      key_binding.input_key = static_cast<ui::VirtualKey>(
          string_util::from_string<uint16_t>(token, true));
    } else if (token.size() == 1 && (token[0] >= 'A' && token[0] <= 'Z') ||
               (token[0] >= '0' && token[0] <= '9')) {
      key_binding.input_key = static_cast<ui::VirtualKey>(token[0]);
    }

    if (key_binding.input_key == ui::VirtualKey::kNone) {
      XELOGW("winkey: failed to parse binding \"{}\" for controller input {}.",
             source_token, description);
      continue;
    }

    key_bindings_.push_back(key_binding);
    XELOGI("winkey: \"{}\" binds key 0x{:X} to controller input {}.",
           source_token, static_cast<uint16_t>(key_binding.input_key),
           description);
  }
}

WinKeyInputDriver::WinKeyInputDriver(xe::ui::Window* window,
                                     size_t window_z_order)
    : InputDriver(window, window_z_order), window_input_listener_(*this) {
#define XE_HID_WINKEY_BINDING(button, description, cvar_name,          \
                              cvar_default_value)                      \
  ParseKeyBinding(xe::ui::VirtualKey::kXInputPad##button, description, \
                  cvars::cvar_name);
#include "winkey_binding_table.inc"
#undef XE_HID_WINKEY_BINDING

  // Register our supported hookable games
  RegisterHookables(hookable_games_);

  // Read bindings file if it exists
  ParseCustomKeyBinding(key_binds_);

  window->AddInputListener(&window_input_listener_, window_z_order);
}

WinKeyInputDriver::~WinKeyInputDriver() {
  window()->RemoveInputListener(&window_input_listener_);
}

X_STATUS WinKeyInputDriver::Setup() { return X_STATUS_SUCCESS; }

X_RESULT WinKeyInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  if (!IsKeyboardForUserEnabled(user_index) && !IsPassthroughEnabled()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (IsPassthroughEnabled()) {
    out_caps->type = X_INPUT_DEVTYPE::XINPUT_DEVTYPE_KEYBOARD;
    out_caps->sub_type = X_INPUT_DEVSUBTYPE::XINPUT_DEVSUBTYPE_USB_KEYBOARD;
    return X_ERROR_SUCCESS;
  }

  out_caps->type = X_INPUT_DEVTYPE::XINPUT_DEVTYPE_GAMEPAD;
  out_caps->sub_type = X_INPUT_DEVSUBTYPE::XINPUT_DEVSUBTYPE_GAMEPAD;
  out_caps->flags = 0;
  out_caps->gamepad.buttons = 0xFFFF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_ly = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_rx = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_ry = (int16_t)0xFFFFu;
  out_caps->vibration.left_motor_speed = 0;
  out_caps->vibration.right_motor_speed = 0;
  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  if (!IsKeyboardForUserEnabled(user_index)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  packet_number_++;

  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
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

  out_state->packet_number = packet_number_;
  out_state->gamepad.buttons = buttons;
  out_state->gamepad.left_trigger = left_trigger;
  out_state->gamepad.right_trigger = right_trigger;
  out_state->gamepad.thumb_lx = thumb_lx;
  out_state->gamepad.thumb_ly = thumb_ly;
  out_state->gamepad.thumb_rx = thumb_rx;
  out_state->gamepad.thumb_ry = thumb_ry;

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

  if (!game_modifier_handled && modifier_pressed) {
    // Modifier not handled by any supported game class, apply default modifier
    // (swap LS input to RS, for games that require RS movement)
    out_state->gamepad.thumb_rx = out_state->gamepad.thumb_lx;
    out_state->gamepad.thumb_ry = out_state->gamepad.thumb_ly;
    out_state->gamepad.thumb_lx = 0;
    out_state->gamepad.thumb_ly = 0;
  }

  if (IsPassthroughEnabled()) {
    memset(out_state, 0, sizeof(out_state));
  }

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  if (!IsKeyboardForUserEnabled(user_index) && !IsPassthroughEnabled()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  if (!is_active()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (!IsKeyboardForUserEnabled(user_index) && !IsPassthroughEnabled()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  // Pop from the queue.
  KeyEvent evt;
  {
    auto global_lock = global_critical_region_.Acquire();
    if (key_events_.empty()) {
      // No keys!
      return X_ERROR_EMPTY;
    }
    evt = key_events_.front();
    key_events_.pop();
  }

  X_RESULT result = X_ERROR_EMPTY;

  ui::VirtualKey xinput_virtual_key = ui::VirtualKey::kNone;
  uint16_t unicode = 0;
  uint16_t keystroke_flags = 0;
  uint8_t hid_code = 0;

  bool capital = IsKeyToggled(VK_CAPITAL) || IsKeyDown(VK_SHIFT);

  if (!IsPassthroughEnabled()) {
    if (IsKeyboardForUserEnabled(user_index)) {
      for (const KeyBinding& b : key_bindings_) {
        if (b.input_key == evt.virtual_key &&
            ((b.lowercase == b.uppercase) || (b.lowercase && !capital) ||
             (b.uppercase && capital))) {
          xinput_virtual_key = b.output_key;
        }
      }
    }
  } else {
    xinput_virtual_key = evt.virtual_key;

    if (capital) {
      keystroke_flags |= 0x0008;  // XINPUT_KEYSTROKE_SHIFT
    }

    if (IsKeyToggled(VK_CONTROL)) {
      keystroke_flags |= 0x0010;  // XINPUT_KEYSTROKE_CTRL
    }

    if (IsKeyToggled(VK_MENU)) {
      keystroke_flags |= 0x0020;  // XINPUT_KEYSTROKE_ALT
    }
  }

  if (xinput_virtual_key != ui::VirtualKey::kNone) {
    if (evt.transition == true) {
      keystroke_flags |= 0x0001;  // XINPUT_KEYSTROKE_KEYDOWN
      if (evt.prev_state == evt.transition) {
        keystroke_flags |= 0x0004;  // XINPUT_KEYSTROKE_REPEAT
      }
    } else if (evt.transition == false) {
      keystroke_flags |= 0x0002;  // XINPUT_KEYSTROKE_KEYUP
    }

    if (IsPassthroughEnabled()) {
      if (GetKeyboardState(key_map_)) {
        WCHAR buf;
        if (ToUnicode(uint8_t(xinput_virtual_key), 0, key_map_, &buf, 1, 0) ==
            1) {
          keystroke_flags |= 0x1000;  // XINPUT_KEYSTROKE_VALIDUNICODE
          unicode = buf;
        }
      }
    }

    result = X_ERROR_SUCCESS;
  }

  out_keystroke->virtual_key = uint16_t(xinput_virtual_key);
  out_keystroke->unicode = unicode;
  out_keystroke->flags = keystroke_flags;
  out_keystroke->user_index = user_index;
  out_keystroke->hid_code = hid_code;

  // X_ERROR_EMPTY if no new keys
  // X_ERROR_DEVICE_NOT_CONNECTED if no device
  // X_ERROR_SUCCESS if key
  return result;
}

void WinKeyInputDriver::WinKeyWindowInputListener::OnKeyDown(ui::KeyEvent& e) {
  driver_.OnKey(e, true);
}

void WinKeyInputDriver::WinKeyWindowInputListener::OnKeyUp(ui::KeyEvent& e) {
  driver_.OnKey(e, false);
}

void WinKeyInputDriver::WinKeyWindowInputListener::OnRawKeyboard(
    ui::KeyEvent& e) {
  driver_.OnRawKeyboard(e);
}

void WinKeyInputDriver::OnRawKeyboard(ui::KeyEvent& e) {
  if (!is_active()) {
    return;
  }

  const auto key = static_cast<uint16_t>(e.virtual_key());

  key_states_[key] = e.prev_state();
}

void WinKeyInputDriver::OnKey(ui::KeyEvent& e, bool is_down) {
  if (!is_active() || static_cast<KeyboardMode>(cvars::keyboard_mode) ==
                          KeyboardMode::Disabled) {
    return;
  }

  KeyEvent key;
  key.virtual_key = e.virtual_key();
  key.transition = is_down;
  key.prev_state = e.prev_state();
  key.repeat_count = e.repeat_count();

  auto global_lock = global_critical_region_.Acquire();
  key_events_.push(key);
}

InputType WinKeyInputDriver::GetInputType() const {
  switch (static_cast<KeyboardMode>(cvars::keyboard_mode)) {
    case KeyboardMode::Disabled:
      return InputType::None;
    case KeyboardMode::Enabled:
      return InputType::Controller;
    case KeyboardMode::Passthrough:
      return InputType::Keyboard;
    default:
      break;
  }
  return InputType::Controller;
}

void WinKeyInputDriver::WinKeyWindowInputListener::OnRawMouse(
    ui::MouseEvent& e) {
  driver_.OnRawMouse(e);
}

void WinKeyInputDriver::OnRawMouse(ui::MouseEvent& evt) {
  if (!is_active()) {
    return;
  }

  OnMouse(evt, mouse_events_, key_states_);
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
