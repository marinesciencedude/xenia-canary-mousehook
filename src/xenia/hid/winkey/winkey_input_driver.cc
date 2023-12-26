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

#include "xenia/hid/winkey/hookables/goldeneye.h"
#include "xenia/hid/winkey/hookables/halo3.h"
#include "xenia/hid/winkey/hookables/SourceEngine.h"
#include "xenia/hid/winkey/hookables/Crackdown2.h"

DEFINE_bool(invert_y, false, "Invert mouse Y axis", "MouseHook");
DEFINE_bool(invert_x, false, "Invert mouse X axis", "MouseHook");
DEFINE_bool(swap_wheel, false,
            "Swaps binds for wheel, so wheel up will go to next weapon & down "
            "will go to prev",
            "MouseHook");
DEFINE_double(sensitivity, 1, "Mouse sensitivity", "MouseHook");
DEFINE_bool(disable_autoaim, true,
            "Disable autoaim in games that support it (currently GE & PD)",
            "MouseHook");
DEFINE_double(source_sniper_sensitivity, 0, "Source Sniper Sensitivity",
              "MouseHook");
DEFINE_int32(walk_orthogonal, 22800, "Joystick movement for forward/backward/left/right shiftwalking, default 22800 equates to 134.99 h.u./s", "MouseHook");
DEFINE_int32(walk_diagonal, 18421, "Joystick movement for diagonal shiftwalking, default 18421 equates to 134.99 h.u./s", "MouseHook");

const uint32_t kTitleIdDefaultBindings = 0;

static const std::unordered_map<std::string, uint32_t> kXInputButtons = {
    {"up", 0x1},
    {"down", 0x2},
    {"left", 0x4},
    {"right", 0x8},

    {"start", 0x10},
    {"back", 0x20},

    {"ls", 0x40},
    {"rs", 0x80},

    {"lb", 0x100},
    {"rb", 0x200},

    {"a", 0x1000},
    {"b", 0x2000},
    {"x", 0x4000},
    {"y", 0x8000},

    {"lt", XINPUT_BIND_LEFT_TRIGGER},
    {"rt", XINPUT_BIND_RIGHT_TRIGGER},

    {"ls-up", XINPUT_BIND_LS_UP},
    {"ls-down", XINPUT_BIND_LS_DOWN},
    {"ls-left", XINPUT_BIND_LS_LEFT},
    {"ls-right", XINPUT_BIND_LS_RIGHT},

    {"rs-up", XINPUT_BIND_RS_UP},
    {"rs-down", XINPUT_BIND_RS_DOWN},
    {"rs-left", XINPUT_BIND_RS_LEFT},
    {"rs-right", XINPUT_BIND_RS_RIGHT},

    {"modifier", XINPUT_BIND_MODIFIER}};

static const std::unordered_map<std::string, uint32_t> kKeyMap = {
    {"lclick", VK_LBUTTON},
    {"lmouse", VK_LBUTTON},
    {"mouse1", VK_LBUTTON},
    {"rclick", VK_RBUTTON},
    {"rmouse", VK_RBUTTON},
    {"mouse2", VK_RBUTTON},
    {"mclick", VK_MBUTTON},
    {"mmouse", VK_MBUTTON},
    {"mouse3", VK_MBUTTON},
    {"mouse4", VK_XBUTTON1},
    {"mouse5", VK_XBUTTON2},
    {"mwheelup", VK_BIND_MWHEELUP},
    {"mwheeldown", VK_BIND_MWHEELDOWN},

    {"control", VK_LCONTROL},
    {"ctrl", VK_LCONTROL},
    {"alt", VK_LMENU},
    {"lcontrol", VK_LCONTROL},
    {"lctrl", VK_LCONTROL},
    {"lalt", VK_LMENU},
    {"rcontrol", VK_RCONTROL},
    {"rctrl", VK_RCONTROL},
    {"altgr", VK_RMENU},
    {"ralt", VK_RMENU},

    {"lshift", VK_LSHIFT},
    {"shift", VK_LSHIFT},
    {"rshift", VK_RSHIFT},

    {"backspace", VK_BACK},
    {"down", VK_DOWN},
    {"left", VK_LEFT},
    {"right", VK_RIGHT},
    {"up", VK_UP},
    {"delete", VK_DELETE},
    {"end", VK_END},
    {"escape", VK_ESCAPE},
    {"home", VK_HOME},
    {"pgdown", VK_NEXT},
    {"pgup", VK_PRIOR},
    {"return", VK_RETURN},
    {"enter", VK_RETURN},
    {"renter", VK_SEPARATOR},
    {"space", VK_SPACE},
    {"tab", VK_TAB},
    {"f1", VK_F1},
    {"f2", VK_F2},
    {"f3", VK_F3},
    {"f4", VK_F4},
    {"f5", VK_F5},
    {"f6", VK_F6},
    {"f7", VK_F7},
    {"f8", VK_F8},
    {"f9", VK_F9},
    {"f10", VK_F10},
    {"f11", VK_F11},
    {"f12", VK_F12},
    {"f13", VK_F13},
    {"f14", VK_F14},
    {"f15", VK_F15},
    {"f16", VK_F16},
    {"f17", VK_F17},
    {"f18", VK_F18},
    {"f19", VK_F19},
    {"f20", VK_F20},
    {"num0", VK_NUMPAD0},
    {"num1", VK_NUMPAD1},
    {"num2", VK_NUMPAD2},
    {"num3", VK_NUMPAD3},
    {"num4", VK_NUMPAD4},
    {"num5", VK_NUMPAD5},
    {"num6", VK_NUMPAD6},
    {"num7", VK_NUMPAD7},
    {"num8", VK_NUMPAD8},
    {"num9", VK_NUMPAD9},
    {"num+", VK_ADD},
    {"num-", VK_SUBTRACT},
    {"num*", VK_MULTIPLY},
    {"num/", VK_DIVIDE},
    {"num.", VK_DECIMAL},
    {"numenter", VK_SEPARATOR},
    {";", VK_OEM_1},
    {":", VK_OEM_1},
    {"=", VK_OEM_PLUS},
    {"+", VK_OEM_PLUS},
    {",", VK_OEM_COMMA},
    {"<", VK_OEM_COMMA},
    {"-", VK_OEM_MINUS},
    {"_", VK_OEM_MINUS},
    {".", VK_OEM_PERIOD},
    {">", VK_OEM_PERIOD},
    {"/", VK_OEM_2},
    {"?", VK_OEM_2},
    {"'", VK_OEM_3},  // uk keyboard
    {"@", VK_OEM_3},  // uk keyboard
    {"[", VK_OEM_4},
    {"{", VK_OEM_4},
    {"\\", VK_OEM_5},
    {"|", VK_OEM_5},
    {"]", VK_OEM_6},
    {"}", VK_OEM_6},
    {"#", VK_OEM_7},  // uk keyboard
    {"\"", VK_OEM_7},
    {"`", VK_OEM_8},  // uk keyboard, no idea what this is on US..
};

const std::string WHITESPACE = " \n\r\t\f\v";

std::string ltrim(const std::string& s) {
  size_t start = s.find_first_not_of(WHITESPACE);
  return (start == std::string::npos) ? "" : s.substr(start);
}

std::string rtrim(const std::string& s) {
  size_t end = s.find_last_not_of(WHITESPACE);
  return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string trim(const std::string& s) { return rtrim(ltrim(s)); }

int ParseButtonCombination(const char* combo) {
  size_t len = strlen(combo);

  int retval = 0;
  std::string cur_token;

  // Parse combo tokens into buttons bitfield (tokens seperated by any
  // non-alphabetical char, eg. +)
  for (size_t i = 0; i < len; i++) {
    char c = combo[i];

    if (!isalpha(c) && c != '-') {
      if (cur_token.length() && kXInputButtons.count(cur_token))
        retval |= kXInputButtons.at(cur_token);

      cur_token.clear();
      continue;
    }
    cur_token += ::tolower(c);
  }

  if (cur_token.length() && kXInputButtons.count(cur_token))
    retval |= kXInputButtons.at(cur_token);

  return retval;
}

#define XE_HID_WINKEY_BINDING(button, description, cvar_name, \
                              cvar_default_value)             \
  DEFINE_string(cvar_name, cvar_default_value,                \
                "List of keys to bind to " description        \
                ", separated by spaces",                      \
                "HID.WinKey")
#include "winkey_binding_table.inc"
#undef XE_HID_WINKEY_BINDING

DEFINE_int32(keyboard_user_index, 0, "Controller port that keyboard emulates",
             "HID.WinKey");

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
  hookable_games_.push_back(std::move(std::make_unique<GoldeneyeGame>()));
  hookable_games_.push_back(std::move(std::make_unique<Halo3Game>()));
  hookable_games_.push_back(std::move(std::make_unique<SourceEngine>()));
  hookable_games_.push_back(std::move(std::make_unique<Crackdown2Game>()));

  // Read bindings file if it exists
  std::ifstream binds("bindings.ini");
  if (!binds.is_open()) {
    xe::ShowSimpleMessageBox(
        xe::SimpleMessageBoxType::Warning,
               "Xenia failed to load bindings.ini file, MouseHook won't have any keys bound!");
  } else {
    std::string cur_section = "default";
    uint32_t cur_game = kTitleIdDefaultBindings;
    std::unordered_map<uint32_t, uint32_t> cur_binds;

    std::string line;
    while (std::getline(binds, line)) {
      line = trim(line);
      if (!line.length()) {
        continue; // blank line
      }
      if (line[0] == ';') {
        continue; // comment
      }

      if (line.length() >= 3 && line[0] == '[' &&
          line[line.length() - 1] == ']') {

        // New section
        if (cur_binds.size() > 0) {
          key_binds_.emplace(cur_game, cur_binds);
          cur_binds.clear();
        }
        cur_section = line.substr(1, line.length() - 2);
        auto sep = cur_section.find_first_of(' ');
        if (sep >= 0) {
          cur_section = cur_section.substr(0, sep);
        }
        cur_game = std::stoul(cur_section, nullptr, 16);

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
      uint32_t key = 0;
      if (kKeyMap.count(key_str)) {
        key = kKeyMap.at(key_str);
      } else {
        if (key_str.length() == 1 &&
            (isalpha(key_str[0]) || isdigit(key_str[0]))) {
          key = (unsigned char)toupper(key_str[0]);
        }
      }

      if (!key) {
        continue;  // unknown key
      }

      // Parse value
      uint32_t value = ParseButtonCombination(val_str.c_str());
      cur_binds.emplace(key, value);
    }
    if (cur_binds.size() > 0) {
      key_binds_.emplace(cur_game, cur_binds);
      cur_binds.clear();
    }
  }

  // Register our event listeners
  window->on_raw_mouse.AddListener([this](ui::MouseEvent& evt) {
    if (!is_active()) {
      return;
    }

    std::unique_lock<std::mutex> mouse_lock(mouse_mutex_);

    MouseEvent mouse;
    mouse.x_delta = evt.x();
    mouse.y_delta = evt.y();
    mouse.buttons = evt.scroll_x();
    mouse.wheel_delta = evt.scroll_y();
    mouse_events_.push(mouse);

    {
      std::unique_lock<std::mutex> key_lock(key_mutex_);
      if (mouse.buttons & RI_MOUSE_LEFT_BUTTON_DOWN) {
        key_states_[VK_LBUTTON] = true;
      }
      if (mouse.buttons & RI_MOUSE_LEFT_BUTTON_UP) {
        key_states_[VK_LBUTTON] = false;
      }
      if (mouse.buttons & RI_MOUSE_RIGHT_BUTTON_DOWN) {
        key_states_[VK_RBUTTON] = true;
      }
      if (mouse.buttons & RI_MOUSE_RIGHT_BUTTON_UP) {
        key_states_[VK_RBUTTON] = false;
      }
      if (mouse.buttons & RI_MOUSE_MIDDLE_BUTTON_DOWN) {
        key_states_[VK_MBUTTON] = true;
      }
      if (mouse.buttons & RI_MOUSE_MIDDLE_BUTTON_UP) {
        key_states_[VK_MBUTTON] = false;
      }
      if (mouse.buttons & RI_MOUSE_BUTTON_4_DOWN) {
        key_states_[VK_XBUTTON1] = true;
      }
      if (mouse.buttons & RI_MOUSE_BUTTON_4_UP) {
        key_states_[VK_XBUTTON1] = false;
      }
      if (mouse.buttons & RI_MOUSE_BUTTON_5_DOWN) {
        key_states_[VK_XBUTTON2] = true;
      }
      if (mouse.buttons & RI_MOUSE_BUTTON_5_UP) {
        key_states_[VK_XBUTTON2] = false;
      }
    }
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
  if (user_index != cvars::keyboard_user_index) {
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
  if (user_index != cvars::keyboard_user_index) {
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

  if (window()->HasFocus() && is_active() && xe::kernel::kernel_state()->has_executable_module()) {
      {
        std::unique_lock<std::mutex> mouse_lock(mouse_mutex_);
        while (!mouse_events_.empty()) {
          auto& mouse = mouse_events_.front();
          state.mouse.x_delta += mouse.x_delta;
          state.mouse.y_delta += mouse.y_delta;
          state.mouse.wheel_delta += mouse.wheel_delta;
          mouse_events_.pop();
        }
      }

      if (state.mouse.wheel_delta != 0) {
        if (cvars::swap_wheel) {
          state.mouse.wheel_delta = -state.mouse.wheel_delta;
        }
      }

      {
        std::unique_lock<std::mutex> key_lock(key_mutex_);
        state.key_states = key_states_;

        // Handle key bindings
        uint32_t cur_game = xe::kernel::kernel_state()->title_id();
        if (!key_binds_.count(cur_game)) {
          cur_game = kTitleIdDefaultBindings;
        }
        if (key_binds_.count(cur_game)) {
          auto& binds = key_binds_.at(cur_game);
          auto process_binding = [binds, &buttons, &left_trigger,
                                  &right_trigger, &thumb_lx, &thumb_ly,
                                  &thumb_rx, &thumb_ry,
                                  &modifier_pressed](uint32_t key) {
            if (!binds.count(key)) {
              return;
            }
            auto binding = binds.at(key);
            buttons |= (binding & XINPUT_BUTTONS_MASK);

            if (binding & XINPUT_BIND_LEFT_TRIGGER) {
              left_trigger = 0xFF;
            }

            if (binding & XINPUT_BIND_RIGHT_TRIGGER) {
              right_trigger = 0xFF;
            }

            if (binding & XINPUT_BIND_LS_UP) {
              thumb_ly = SHRT_MAX;
            }
            if (binding & XINPUT_BIND_LS_DOWN) {
              thumb_ly = SHRT_MIN;
            }
            if (binding & XINPUT_BIND_LS_LEFT) {
              thumb_lx = SHRT_MIN;
            }
            if (binding & XINPUT_BIND_LS_RIGHT) {
              thumb_lx = SHRT_MAX;
            }

            if (binding & XINPUT_BIND_RS_UP) {
              thumb_ry = SHRT_MAX;
            }
            if (binding & XINPUT_BIND_RS_DOWN) {
              thumb_ry = SHRT_MIN;
            }
            if (binding & XINPUT_BIND_RS_LEFT) {
              thumb_rx = SHRT_MIN;
            }
            if (binding & XINPUT_BIND_RS_RIGHT) {
              thumb_rx = SHRT_MAX;
            }

            if (binding & XINPUT_BIND_MODIFIER) {
              modifier_pressed = true;
            }
          };

          if (state.mouse.wheel_delta != 0) {
            if (state.mouse.wheel_delta > 0) {
              process_binding(VK_BIND_MWHEELUP);
            } else {
              process_binding(VK_BIND_MWHEELDOWN);
            }
          }

          for (int i = 0; i < 0x100; i++) {
            if (key_states_[i]) {
              process_binding(i);
            }
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
  if (user_index != cvars::keyboard_user_index) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  if (user_index != cvars::keyboard_user_index) {
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
