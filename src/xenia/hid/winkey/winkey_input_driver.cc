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
#include "xenia/ui/window.h"

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
using namespace xe::string_util;

bool __inline IsKeyToggled(uint8_t key) {
  return (GetKeyState(key) & 0x1) == 0x1;
}

bool __inline IsKeyDown(uint8_t key) {
  return (GetAsyncKeyState(key) & 0x8000) == 0x8000;
}

bool __inline IsKeyDown(ui::VirtualKey virtual_key) {
  return IsKeyDown(static_cast<uint8_t>(virtual_key));
}

// Lookup the value of xinput string
static const std::map<std::string, ui::VirtualKey> kXInputButtons = {
    {"up", ui::VirtualKey::kXInputPadDpadUp},
    {"down", ui::VirtualKey::kXInputPadDpadDown},
    {"left", ui::VirtualKey::kXInputPadDpadLeft},
    {"right", ui::VirtualKey::kXInputPadDpadRight},

    {"start", ui::VirtualKey::kXInputPadStart},
    {"back", ui::VirtualKey::kXInputPadBack},
    {"guide", ui::VirtualKey::kXInputPadGuide},

    {"ls", ui::VirtualKey::kXInputPadLThumbPress},
    {"rs", ui::VirtualKey::kXInputPadRThumbPress},

    {"lb", ui::VirtualKey::kXInputPadLShoulder},
    {"rb", ui::VirtualKey::kXInputPadRShoulder},

    {"a", ui::VirtualKey::kXInputPadA},
    {"b", ui::VirtualKey::kXInputPadB},
    {"x", ui::VirtualKey::kXInputPadX},
    {"y", ui::VirtualKey::kXInputPadY},

    {"lt", ui::VirtualKey::kXInputPadLTrigger},
    {"rt", ui::VirtualKey::kXInputPadRTrigger},

    {"ls-up", ui::VirtualKey::kXInputPadLThumbUp},
    {"ls-down", ui::VirtualKey::kXInputPadLThumbDown},
    {"ls-left", ui::VirtualKey::kXInputPadLThumbLeft},
    {"ls-right", ui::VirtualKey::kXInputPadLThumbRight},

    {"rs-up", ui::VirtualKey::kXInputPadRThumbUp},
    {"rs-down", ui::VirtualKey::kXInputPadRThumbDown},
    {"rs-left", ui::VirtualKey::kXInputPadRThumbLeft},
    {"rs-right", ui::VirtualKey::kXInputPadRThumbRight},

    {"modifier", ui::VirtualKey::kModifier}};

// Lookup the value of key string
static const std::map<std::string, ui::VirtualKey> kKeyMap = {
    {"control", ui::VirtualKey::kLControl},
    {"ctrl", ui::VirtualKey::kLControl},
    {"alt", ui::VirtualKey::kLMenu},
    {"lcontrol", ui::VirtualKey::kLControl},
    {"lctrl", ui::VirtualKey::kLControl},
    {"lalt", ui::VirtualKey::kLMenu},
    {"rcontrol", ui::VirtualKey::kRControl},
    {"rctrl", ui::VirtualKey::kRControl},
    {"altgr", ui::VirtualKey::kRMenu},
    {"ralt", ui::VirtualKey::kRMenu},

    {"lshift", ui::VirtualKey::kLShift},
    {"shift", ui::VirtualKey::kLShift},
    {"rshift", ui::VirtualKey::kRShift},

    {"backspace", ui::VirtualKey::kBack},
    {"down", ui::VirtualKey::kDown},
    {"left", ui::VirtualKey::kLeft},
    {"right", ui::VirtualKey::kRight},
    {"up", ui::VirtualKey::kUp},
    {"delete", ui::VirtualKey::kDelete},
    {"end", ui::VirtualKey::kEnd},
    {"escape", ui::VirtualKey::kEscape},
    {"home", ui::VirtualKey::kHome},
    {"pgdown", ui::VirtualKey::kNext},
    {"pgup", ui::VirtualKey::kPrior},
    {"return", ui::VirtualKey::kReturn},
    {"enter", ui::VirtualKey::kReturn},
    {"renter", ui::VirtualKey::kSeparator},
    {"space", ui::VirtualKey::kSpace},
    {"tab", ui::VirtualKey::kTab},
    {"f1", ui::VirtualKey::kF1},
    {"f2", ui::VirtualKey::kF2},
    {"f3", ui::VirtualKey::kF3},
    {"f4", ui::VirtualKey::kF4},
    {"f5", ui::VirtualKey::kF5},
    {"f6", ui::VirtualKey::kF6},
    {"f7", ui::VirtualKey::kF7},
    {"f8", ui::VirtualKey::kF8},
    {"f9", ui::VirtualKey::kF9},
    {"f10", ui::VirtualKey::kF10},
    {"f11", ui::VirtualKey::kF11},
    {"f12", ui::VirtualKey::kF12},
    {"f13", ui::VirtualKey::kF13},
    {"f14", ui::VirtualKey::kF14},
    {"f15", ui::VirtualKey::kF15},
    {"f16", ui::VirtualKey::kF16},
    {"f17", ui::VirtualKey::kF17},
    {"f18", ui::VirtualKey::kF18},
    {"f19", ui::VirtualKey::kF19},
    {"f20", ui::VirtualKey::kF10},
    {"num0", ui::VirtualKey::kNumpad0},
    {"num1", ui::VirtualKey::kNumpad1},
    {"num2", ui::VirtualKey::kNumpad2},
    {"num3", ui::VirtualKey::kNumpad3},
    {"num4", ui::VirtualKey::kNumpad4},
    {"num5", ui::VirtualKey::kNumpad5},
    {"num6", ui::VirtualKey::kNumpad6},
    {"num7", ui::VirtualKey::kNumpad7},
    {"num8", ui::VirtualKey::kNumpad8},
    {"num9", ui::VirtualKey::kNumpad9},
    {"num+", ui::VirtualKey::kAdd},
    {"num-", ui::VirtualKey::kSubtract},
    {"num*", ui::VirtualKey::kMultiply},
    {"num/", ui::VirtualKey::kDivide},
    {"num.", ui::VirtualKey::kDecimal},
    {"numenter", ui::VirtualKey::kSeparator},
    {";", ui::VirtualKey::kOem1},
    {":", ui::VirtualKey::kOem1},
    {"=", ui::VirtualKey::kOemPlus},
    {"+", ui::VirtualKey::kOemPlus},
    {",", ui::VirtualKey::kOemComma},
    {"<", ui::VirtualKey::kOemComma},
    {"-", ui::VirtualKey::kOemMinus},
    {"_", ui::VirtualKey::kOemMinus},
    {".", ui::VirtualKey::kOemPeriod},
    {">", ui::VirtualKey::kOemPeriod},
    {"/", ui::VirtualKey::kOem2},
    {"?", ui::VirtualKey::kOem2},
    {"'", ui::VirtualKey::kOem3},  // uk keyboard
    {"@", ui::VirtualKey::kOem3},  // uk keyboard
    {"[", ui::VirtualKey::kOem4},
    {"{", ui::VirtualKey::kOem4},
    {"\\", ui::VirtualKey::kOem5},
    {"|", ui::VirtualKey::kOem5},
    {"]", ui::VirtualKey::kOem6},
    {"}", ui::VirtualKey::kOem6},
    {"#", ui::VirtualKey::kOem7},  // uk keyboard
    {"\"", ui::VirtualKey::kOem7},
    {"`", ui::VirtualKey::kOem8},  // uk keyboard, no idea what this is on US..
};

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

ui::VirtualKey WinKeyInputDriver::ParseButtonCombination(const char* combo) {
  size_t len = strlen(combo);

  uint16_t retval = 0;
  std::string cur_token;

  // Parse combo tokens into buttons bitfield (tokens separated by any
  // non-alphabetical char, eg. +)
  for (size_t i = 0; i < len; i++) {
    char c = combo[i];

    if (!isalpha(c) && c != '-') {
      if (cur_token.length() && kXInputButtons.count(cur_token))
        retval |= static_cast<uint16_t>(kXInputButtons.at(cur_token));

      cur_token.clear();
      continue;
    }
    cur_token += ::tolower(c);
  }

  if (cur_token.length() && kXInputButtons.count(cur_token))
    retval |= static_cast<uint16_t>(kXInputButtons.at(cur_token));

  return static_cast<ui::VirtualKey>(retval);
}

void WinKeyInputDriver::ParseCustomKeyBinding(
    const std::string_view bindings_file) {
  if (!std::filesystem::exists(bindings_file)) {
    return;
  }

  // Read bindings file if it exists
  std::ifstream binds(bindings_file.data());

  std::string cur_section = "default";
  uint32_t title_id = 0;

  std::map<ui::VirtualKey, ui::VirtualKey> cur_binds;

  std::string line;
  while (std::getline(binds, line)) {
    line = trim(line);
    if (!line.length()) {
      continue;  // blank line
    }
    if (line[0] == ';') {
      continue;  // comment
    }

    if (line.length() >= 3 && line[0] == '[' &&
        line[line.length() - 1] == ']') {
      // New section
      if (cur_binds.size() > 0) {
        key_binds_.emplace(title_id, cur_binds);
        cur_binds.clear();
      }

      cur_section = line.substr(1, line.length() - 2);
      auto sep = cur_section.find_first_of(' ');
      if (sep >= 0) {
        cur_section = cur_section.substr(0, sep);
      }

      title_id = std::stoul(cur_section, nullptr, 16);

      continue;
    }

    // Not a section, must be bind
    auto sep = line.find_last_of('=');
    if (sep < 0) {
      continue;  // invalid
    }

    auto key_str = trim(line.substr(0, sep));
    auto val_str = trim(line.substr(sep + 1));

    // key tolower
    std::transform(key_str.begin(), key_str.end(), key_str.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Parse key
    ui::VirtualKey key = ui::VirtualKey::kNone;

    if (kKeyMap.count(key_str)) {
      key = kKeyMap.at(key_str);
    } else if (key_str.length() == 1 &&
               (isalpha(key_str[0]) || isdigit(key_str[0]))) {
      key = static_cast<ui::VirtualKey>(toupper(key_str[0]));
    }

    if (key == ui::VirtualKey::kNone) {
      continue;  // unknown key
    }

    // Parse value
    auto const value = ParseButtonCombination(val_str.c_str());
    cur_binds.emplace(key, value);
  }

  if (cur_binds.size() > 0) {
    key_binds_.emplace(title_id, cur_binds);
    cur_binds.clear();
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

  auto path = std::filesystem::current_path() / "bindings.ini";

  ParseCustomKeyBinding(path.string());

  window->AddInputListener(&window_input_listener_, window_z_order);
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

  if (window()->HasFocus() && is_active()) {
    for (int i = 0; i < sizeof(key_states_); i++) {
      if (key_states_[i]) {
        const auto& binds = key_binds_.at(title_id);
        const auto vk_key = static_cast<ui::VirtualKey>(i);

        if (!binds.count(vk_key)) {
          break;
        }

        const auto key_binding = binds.at(vk_key);

        // use if instead for binding combinations?
        switch (key_binding) {
          case ui::VirtualKey::kXInputPadA:
            buttons |= X_INPUT_GAMEPAD_A;
            break;
          case ui::VirtualKey::kXInputPadY:
            buttons |= X_INPUT_GAMEPAD_Y;
            break;
          case ui::VirtualKey::kXInputPadB:
            buttons |= X_INPUT_GAMEPAD_B;
            break;
          case ui::VirtualKey::kXInputPadX:
            buttons |= X_INPUT_GAMEPAD_X;
            break;
          case ui::VirtualKey::kXInputPadGuide:
            buttons |= X_INPUT_GAMEPAD_GUIDE;
            break;
          case ui::VirtualKey::kXInputPadDpadLeft:
            buttons |= X_INPUT_GAMEPAD_DPAD_LEFT;
            break;
          case ui::VirtualKey::kXInputPadDpadRight:
            buttons |= X_INPUT_GAMEPAD_DPAD_RIGHT;
            break;
          case ui::VirtualKey::kXInputPadDpadDown:
            buttons |= X_INPUT_GAMEPAD_DPAD_DOWN;
            break;
          case ui::VirtualKey::kXInputPadDpadUp:
            buttons |= X_INPUT_GAMEPAD_DPAD_UP;
            break;
          case ui::VirtualKey::kXInputPadRThumbPress:
            buttons |= X_INPUT_GAMEPAD_RIGHT_THUMB;
            break;
          case ui::VirtualKey::kXInputPadLThumbPress:
            buttons |= X_INPUT_GAMEPAD_LEFT_THUMB;
            break;
          case ui::VirtualKey::kXInputPadBack:
            buttons |= X_INPUT_GAMEPAD_BACK;
            break;
          case ui::VirtualKey::kXInputPadStart:
            buttons |= X_INPUT_GAMEPAD_START;
            break;
          case ui::VirtualKey::kXInputPadLShoulder:
            buttons |= X_INPUT_GAMEPAD_LEFT_SHOULDER;
            break;
          case ui::VirtualKey::kXInputPadRShoulder:
            buttons |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
            break;
          case ui::VirtualKey::kXInputPadLTrigger:
            left_trigger = 0xFF;
            break;
          case ui::VirtualKey::kXInputPadRTrigger:
            right_trigger = 0xFF;
            break;
          case ui::VirtualKey::kXInputPadLThumbLeft:
            thumb_lx += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadLThumbRight:
            thumb_lx += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadLThumbDown:
            thumb_ly += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadLThumbUp:
            thumb_ly += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbUp:
            thumb_ry += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbDown:
            thumb_ry += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadRThumbRight:
            thumb_rx += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbLeft:
            thumb_rx += SHRT_MIN;
            break;
          case ui::VirtualKey::kModifier:
            modifier_pressed = true;
            break;
        }
      }
    }
  }

  out_state->packet_number = packet_number_;
  out_state->gamepad.buttons = buttons;
  out_state->gamepad.left_trigger = left_trigger;
  out_state->gamepad.right_trigger = right_trigger;
  out_state->gamepad.thumb_lx = thumb_lx;
  out_state->gamepad.thumb_ly = thumb_ly;
  out_state->gamepad.thumb_rx = thumb_rx;
  out_state->gamepad.thumb_ry = thumb_ry;

  if (modifier_pressed) {
    // Swap LS input to RS
    out_state->gamepad.thumb_rx = out_state->gamepad.thumb_lx;
    out_state->gamepad.thumb_ry = out_state->gamepad.thumb_ly;
    out_state->gamepad.thumb_lx = 0;
    out_state->gamepad.thumb_ly = 0;
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

  ui::VirtualKey xinput_virtual_key = ui::VirtualKey::kNone;
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

  bool capital = IsKeyToggled(VK_CAPITAL) || IsKeyDown(VK_SHIFT);
  for (const KeyBinding& b : key_bindings_) {
    if (b.input_key == evt.virtual_key &&
        ((b.lowercase == b.uppercase) || (b.lowercase && !capital) ||
         (b.uppercase && capital))) {
      xinput_virtual_key = b.output_key;
    }
  }

  if (xinput_virtual_key != ui::VirtualKey::kNone) {
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

  out_keystroke->virtual_key = uint16_t(xinput_virtual_key);
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
  if (!is_active()) {
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

}  // namespace winkey
}  // namespace hid
}  // namespace xe
