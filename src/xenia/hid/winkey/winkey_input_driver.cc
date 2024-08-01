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

#include "xenia/hid/winkey/hookables/Crackdown2.h"
#include "xenia/hid/winkey/hookables/SaintsRow.h"
#include "xenia/hid/winkey/hookables/SourceEngine.h"
#include "xenia/hid/winkey/hookables/goldeneye.h"
#include "xenia/hid/winkey/hookables/halo3.h"

DEFINE_bool(invert_y, false, "Invert mouse Y axis", "MouseHook");
DEFINE_bool(invert_x, false, "Invert mouse X axis", "MouseHook");
DEFINE_bool(swap_wheel, false,
            "Swaps binds for wheel, so wheel up will go to next weapon & down "
            "will go to prev",
            "MouseHook");
DEFINE_double(sensitivity, 1, "Mouse sensitivity", "MouseHook");
DEFINE_bool(disable_autoaim, true,
            "Disable autoaim in games that support it (currently GE,PD and SR)",
            "MouseHook");
DEFINE_double(source_sniper_sensitivity, 0, "Source Sniper Sensitivity",
              "MouseHook");
DEFINE_int32(walk_orthogonal, 22800,
             "Joystick movement for forward/backward/left/right shiftwalking, "
             "default 22800 equates to 134.99 h.u./s",
             "MouseHook");
DEFINE_int32(walk_diagonal, 18421,
             "Joystick movement for diagonal shiftwalking, default 18421 "
             "equates to 134.99 h.u./s",
             "MouseHook");

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

static const std::unordered_map<std::string, uint32_t> kXInputButtons = {
    {"up", XINPUT_BIND_UP},
    {"down", XINPUT_BIND_DOWN},
    {"left", XINPUT_BIND_LEFT},
    {"right", XINPUT_BIND_RIGHT},

    {"start", XINPUT_BIND_START},
    {"back", XINPUT_BIND_BACK},

    {"ls", XINPUT_BIND_LS},
    {"rs", XINPUT_BIND_RS},

    {"lb", XINPUT_BIND_LB},
    {"rb", XINPUT_BIND_RB},

    {"a", XINPUT_BIND_A},
    {"b", XINPUT_BIND_B},
    {"x", XINPUT_BIND_X},
    {"y", XINPUT_BIND_Y},

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
// Lookup the value of key string
static const std::map<std::string, ui::VirtualKey> kKeyMap = {
    {"lclick", ui::VirtualKey::kLButton},
    {"lmouse", ui::VirtualKey::kLButton},
    {"mouse1", ui::VirtualKey::kLButton},
    {"rclick", ui::VirtualKey::kRButton},
    {"rmouse", ui::VirtualKey::kRButton},
    {"mouse2", ui::VirtualKey::kRButton},
    {"mclick", ui::VirtualKey::kMButton},
    {"mmouse", ui::VirtualKey::kMButton},
    {"mouse3", ui::VirtualKey::kMButton},
    {"mouse4", ui::VirtualKey::kXButton1},
    {"mouse5", ui::VirtualKey::kXButton2},
    {"mwheelup", ui::VirtualKey::kMWheelUp},
    {"mwheeldown", ui::VirtualKey::kMWheelDown},

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

int WinKeyInputDriver::ParseButtonCombination(const char* combo) {
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

void WinKeyInputDriver::ParseCustomKeyBinding(
    const std::string_view bindings_file) {
  if (!std::filesystem::exists(bindings_file)) {
    xe::ShowSimpleMessageBox(xe::SimpleMessageBoxType::Warning,
                             "Xenia failed to load bindings.ini file, "
                             "MouseHook won't have any keys bound!");
    return;
  }

  // Read bindings file if it exists
  std::ifstream binds(bindings_file.data());

  std::string cur_section = "default";
  uint32_t title_id = 0;
  uint32_t prev_title_id = 0;
  std::string cur_type = "Default";

  std::map<ui::VirtualKey, uint32_t> cur_binds;
  std::map<std::string, std::map<ui::VirtualKey, uint32_t>> cur_title_binds;

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
        cur_title_binds.emplace(cur_type, cur_binds);
        cur_binds.clear();
      }

      cur_section = line.substr(1, line.length() - 2);
      auto sep = cur_section.find_first_of(' ');
      if (sep >= 0) {
        cur_section = cur_section.substr(0, sep);
      }

      title_id = std::stoul(cur_section, nullptr, 16);

      if (prev_title_id != title_id) {
        key_binds_.emplace(prev_title_id, cur_title_binds);
        cur_title_binds.clear();
        prev_title_id = title_id;
      }

      cur_section = line.substr(sep + 2, line.length() - 2);
      auto divider = cur_section.find_first_of('-');
      if (divider > 0) {
        cur_type = cur_section.substr(0, divider - 1);
      } else {
        cur_type =
            "Default";  // backwards compatibility with old section headers
      }

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
  hookable_games_.push_back(std::move(std::make_unique<GoldeneyeGame>()));
  hookable_games_.push_back(std::move(std::make_unique<Halo3Game>()));
  hookable_games_.push_back(std::move(std::make_unique<SourceEngine>()));
  hookable_games_.push_back(std::move(std::make_unique<Crackdown2Game>()));
  hookable_games_.push_back(std::move(std::make_unique<SaintsRowGame>()));

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

  if (window()->HasFocus() && is_active()) {
    {
      while (!mouse_events_.empty()) {
        MouseEvent* mouse = &mouse_events_.front();

        if (mouse != nullptr) {
          state.mouse.x_delta += mouse->x_delta;
          state.mouse.y_delta += mouse->y_delta;
          state.mouse.wheel_delta += mouse->wheel_delta;
          mouse_events_.pop();
        }
      }
    }

    for (int i = 0; i < sizeof(key_states_); i++) {
      if (key_states_[i]) {
        std::map<ui::VirtualKey, uint32_t> binds;

        if (key_binds_.find(title_id) == key_binds_.end()) {
          binds = key_binds_.at(0).at("Default");
        } else {
          if (key_binds_.at(title_id).size() > 1) {
            for (auto& game : hookable_games_) {
              if (game->IsGameSupported()) {
                binds = key_binds_.at(title_id).at(game->ChooseBinds());
                break;
              }
            }
          } else {
            binds = key_binds_.at(title_id).at("Default");
          }
        }

        const auto vk_key = static_cast<ui::VirtualKey>(i);

        if (!binds.count(vk_key)) {
          break;
        }

        const auto binding = binds.at(vk_key);

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
  if (title_id) {
    for (auto& game : hookable_games_) {
      if (game->IsGameSupported()) {
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
    out_state->gamepad.thumb_lx = 0;
    out_state->gamepad.thumb_ly = 0;
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

void WinKeyInputDriver::WinKeyWindowInputListener::OnRawMouse(
    ui::MouseEvent& e) {
  driver_.OnRawMouse(e);
}

void WinKeyInputDriver::OnRawMouse(ui::MouseEvent& evt) {
  if (!is_active()) {
    return;
  }

  MouseEvent mouse;
  mouse.x_delta = evt.x();
  mouse.y_delta = evt.y();
  mouse.buttons = evt.scroll_x();
  mouse.wheel_delta = evt.scroll_y();
  mouse_events_.push(mouse);

  {
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
    if (mouse.wheel_delta != 0) {
      if (!cvars::swap_wheel) {
        if (mouse.wheel_delta > 0) {
          key_states_[VK_BIND_MWHEELUP] = true;
        } else {
          key_states_[VK_BIND_MWHEELDOWN] = true;
        }
      } else {
        if (mouse.wheel_delta > 0) {
          key_states_[VK_BIND_MWHEELDOWN] = true;
        } else {
          key_states_[VK_BIND_MWHEELUP] = true;
        }
      }
    }
    if (mouse.wheel_delta == 0) {
      key_states_[VK_BIND_MWHEELUP] = false;
      key_states_[VK_BIND_MWHEELDOWN] = false;
    }
  }
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
