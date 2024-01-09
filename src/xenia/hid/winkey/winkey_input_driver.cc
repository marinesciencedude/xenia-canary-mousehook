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
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_win.h"
#include "xenia/base/system.h"

#include "xenia/kernel/kernel_state.h"

#include "xenia/hid/hookables/mousehook.h"

const uint32_t kTitleIdDefaultBindings = 0;

#define XE_HID_WINKEY_BINDING(button, description, cvar_name, \
                              cvar_default_value)             \
  DEFINE_string(cvar_name, cvar_default_value,                \
                "List of keys to bind to " description        \
                ", separated by spaces",                      \
                "HID.WinKey")
#include "winkey_binding_table.inc"
#undef XE_HID_WINKEY_BINDING

namespace xe {
namespace hid {
namespace winkey {

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
           source_token, key_binding.input_key, description);
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

  memset(key_states_, 0, 256);

  // Register our supported hookable games
  RegisterHookables(hookable_games_);

  // Read bindings file if it exists
  ReadBindings(kTitleIdDefaultBindings, key_binds_);

  // Register our event listeners
  window->on_raw_mouse.AddListener([this](ui::MouseEvent& evt) { 
    if (!is_active()) {
      return;
    }

    RegisterMouseListener(evt, &mouse_mutex_, mouse_events_, &key_mutex_, key_states_); 
  });

  window->on_raw_keyboard.AddListener([this, window](ui::KeyEvent& evt) {
    if (!is_active()) {
      return;
    }

    std::unique_lock<std::mutex> key_lock(key_mutex_);
    key_states_[evt.key_code() & 0xFF] = evt.prev_state();
  });
  
  window->AddInputListener(&window_input_listener_, window_z_order);

  window->on_key_down.AddListener([this](ui::KeyEvent& evt) {
    if (!is_active()) {
      return;
    }
    auto global_lock = global_critical_region_.Acquire();

    KeyEvent key;
    key.vkey = evt.key_code();
    key.transition = true;
    key.prev_state = evt.prev_state();
    key.repeat_count = evt.repeat_count();
    key_events_.push(key);
  });
  window->on_key_up.AddListener([this](ui::KeyEvent& evt) {
    if (!is_active()) {
      return;
    }
    auto global_lock = global_critical_region_.Acquire();

    KeyEvent key;
    key.vkey = evt.key_code();
    key.transition = false;
    key.prev_state = evt.prev_state();
    key.repeat_count = evt.repeat_count();
    key_events_.push(key);
  });
}

WinKeyInputDriver::~WinKeyInputDriver() {
  window()->RemoveInputListener(&window_input_listener_);
}

X_STATUS WinKeyInputDriver::Setup() { return X_STATUS_SUCCESS; }

X_RESULT WinKeyInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // TODO(benvanik): confirm with a real XInput controller.
  out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
  out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
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

#define IS_KEY_DOWN(key) (key_states_[key])

X_RESULT WinKeyInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  if (user_index != 0) {
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

  X_RESULT result = X_ERROR_SUCCESS;

  RawInputState state;

  if (window()->HasFocus() && is_active() && xe::kernel::kernel_state()->has_executable_module())
  {
    HandleKeyBindings(state, mouse_events_, &mouse_mutex_, &key_mutex_,
                      key_states_, key_binds_, kTitleIdDefaultBindings, &buttons, &left_trigger,
                      &right_trigger, &thumb_lx, &thumb_ly, &thumb_rx,
                      &thumb_ry, &modifier_pressed);
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
    if (xe::kernel::kernel_state()->has_executable_module())
    {
        for (auto& game : hookable_games_) {
          if (game->IsGameSupported()) {
            std::unique_lock<std::mutex> key_lock(key_mutex_);
            game->DoHooks(user_index, state, out_state);
            if (modifier_pressed) {
              game_modifier_handled =
                  game->ModifierKeyHandler(user_index, state, out_state);
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
    out_state->gamepad.thumb_lx = out_state->gamepad.thumb_ly = 0;
  }

  return result;
}

X_RESULT WinKeyInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (!is_active()) {
    return X_ERROR_EMPTY;
  }

  X_RESULT result = X_ERROR_EMPTY;

  uint16_t virtual_key = 0;
  uint16_t unicode = 0;
  uint16_t keystroke_flags = 0;
  uint8_t hid_code = 0;

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
  
  // left stick
  if (evt.vkey == (0x57)) {
    // W
    virtual_key = 0x5820;  // VK_PAD_LTHUMB_UP
  }
  if (evt.vkey == (0x53)) {
    // S
    virtual_key = 0x5821;  // VK_PAD_LTHUMB_DOWN
  }
  if (evt.vkey == (0x44)) {
    // D
    virtual_key = 0x5822;  // VK_PAD_LTHUMB_RIGHT
  }
  if (evt.vkey == (0x41)) {
    // A
    virtual_key = 0x5823;  // VK_PAD_LTHUMB_LEFT
  }

  // Right stick
  if (evt.vkey == (0x26)) {
    // Up
    virtual_key = 0x5830;
  }
  if (evt.vkey == (0x28)) {
    // Down
    virtual_key = 0x5831;
  }
  if (evt.vkey == (0x27)) {
    // Right
    virtual_key = 0x5832;
  }
  if (evt.vkey == (0x25)) {
    // Left
    virtual_key = 0x5833;
  }

  if (evt.vkey == (0x4C)) {
    // L
    virtual_key = 0x5802;  // VK_PAD_X
  } else if (evt.vkey == (VK_OEM_7)) {
    // '
    virtual_key = 0x5801;  // VK_PAD_B
  } else if (evt.vkey == (VK_OEM_1)) {
    // ;
    virtual_key = 0x5800;  // VK_PAD_A
  } else if (evt.vkey == (0x50)) {
    // P
    virtual_key = 0x5803;  // VK_PAD_Y
  }

  if (evt.vkey == (0x58)) {
    // X
    virtual_key = 0x5814;  // VK_PAD_START
  }
  if (evt.vkey == (0x5A)) {
    // Z
    virtual_key = 0x5815;  // VK_PAD_BACK
  }
  if (evt.vkey == (0x51) || evt.vkey == (0x49)) {
    // Q / I
    virtual_key = 0x5806;  // VK_PAD_LTRIGGER
  }
  if (evt.vkey == (0x45) || evt.vkey == (0x4F)) {
    // E / O
    virtual_key = 0x5807;  // VK_PAD_RTRIGGER
  }
  if (evt.vkey == (0x31)) {
    // 1
    virtual_key = 0x5805;  // VK_PAD_LSHOULDER
  }
  if (evt.vkey == (0x33)) {
    // 3
    virtual_key = 0x5804;  // VK_PAD_RSHOULDER
  }

  if (virtual_key != 0) {
    if (evt.transition == true) {
      keystroke_flags |= 0x0001;  // XINPUT_KEYSTROKE_KEYDOWN
    } else if (evt.transition == false) {
      keystroke_flags |= 0x0002;  // XINPUT_KEYSTROKE_KEYUP
    }

    if (evt.prev_state == evt.transition) {
      keystroke_flags |= 0x0004;  // XINPUT_KEYSTROKE_REPEAT
    }

    result = X_ERROR_SUCCESS;
  }

  out_keystroke->virtual_key = virtual_key;
  out_keystroke->unicode = unicode;
  out_keystroke->flags = keystroke_flags;
  out_keystroke->user_index = 0;
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

void WinKeyInputDriver::OnKey(ui::KeyEvent& e, bool is_down) {
  if (!is_active()) {
    return;
  }

  KeyEvent key;
  key.vkey = e.key_code();
  key.transition = is_down;
  key.prev_state = e.prev_state();
  key.repeat_count = e.repeat_count();

  auto global_lock = global_critical_region_.Acquire();
  key_events_.push(key);
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
