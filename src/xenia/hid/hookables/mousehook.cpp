#include "xenia/hid/hookables/mousehook.h"

#include "xenia/hid/hookables/Crackdown2.h"
#include "xenia/hid/hookables/SourceEngine.h"
#include "xenia/hid/hookables/goldeneye.h"
#include "xenia/hid/hookables/halo3.h"

#include "xenia/base/system.h"
#include "xenia/hid/input_driver.h"
#include "xenia/kernel/util/shim_utils.h"

DECLARE_bool(swap_wheel);

namespace xe {
namespace hid {

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

void RegisterHookables(
    std::vector<std::unique_ptr<HookableGame>>& hookable_games_) {
  hookable_games_.push_back(std::move(std::make_unique<GoldeneyeGame>()));
  hookable_games_.push_back(std::move(std::make_unique<Halo3Game>()));
  hookable_games_.push_back(std::move(std::make_unique<SourceEngine>()));
  hookable_games_.push_back(std::move(std::make_unique<Crackdown2Game>()));
}

void ReadBindings(
    uint32_t kTitleIdDefaultBindings,
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>>&
        key_binds_) {
  std::ifstream binds("bindings.ini");
  if (!binds.is_open()) {
    xe::ShowSimpleMessageBox(
        xe::SimpleMessageBoxType::Warning,
        "Xenia failed to load bindings.ini file, MouseHook "
        "won't have any keys bound!");
  } else {
    std::string cur_section = "default";
    uint32_t cur_game = kTitleIdDefaultBindings;
    std::unordered_map<uint32_t, uint32_t> cur_binds;

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
}

void RegisterMouseListener(ui::MouseEvent& evt, std::mutex* mouse_mutex_,
                           std::queue<MouseEvent>& mouse_events_,
                           std::mutex* key_mutex_, bool (&key_states_)[256]) {
  std::unique_lock<std::mutex> mouse_lock(*mouse_mutex_);

  MouseEvent mouse;
  mouse.x_delta = evt.x();
  mouse.y_delta = evt.y();
  mouse.buttons = evt.scroll_x();
  mouse.wheel_delta = evt.scroll_y();
  mouse_events_.push(mouse);

  {
    std::unique_lock<std::mutex> key_lock(*key_mutex_);
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
}

void HandleKeyBindings(
    RawInputState& state, std::queue<MouseEvent>& mouse_events_,
    std::mutex* mouse_mutex_, std::mutex* key_mutex_, bool (&key_states_)[256],
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>>&
        key_binds_,
    uint32_t kTitleIdDefaultBindings, uint16_t* buttons, uint8_t* left_trigger,
    uint8_t* right_trigger, int16_t* thumb_lx, int16_t* thumb_ly,
    int16_t* thumb_rx, int16_t* thumb_ry, bool* modifier_pressed) {
  {
    std::unique_lock<std::mutex> mouse_lock(*mouse_mutex_);
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
    std::unique_lock<std::mutex> key_lock(*key_mutex_);
    state.key_states = key_states_;

    // Handle key bindings
    uint32_t cur_game = xe::kernel::kernel_state()->title_id();
    if (!key_binds_.count(cur_game)) {
      cur_game = kTitleIdDefaultBindings;
    }
    if (key_binds_.count(cur_game)) {
      auto& binds = key_binds_.at(cur_game);
      auto process_binding = [binds, &buttons, &left_trigger, &right_trigger,
                              &thumb_lx, &thumb_ly, &thumb_rx, &thumb_ry,
                              &modifier_pressed](uint32_t key) {
        if (!binds.count(key)) {
          return;
        }
        auto binding = binds.at(key);
        *buttons |= (binding & XINPUT_BUTTONS_MASK);

        if (binding & XINPUT_BIND_LEFT_TRIGGER) {
          *left_trigger = 0xFF;
        }

        if (binding & XINPUT_BIND_RIGHT_TRIGGER) {
          *right_trigger = 0xFF;
        }

        if (binding & XINPUT_BIND_LS_UP) {
          *thumb_ly = SHRT_MAX;
        }
        if (binding & XINPUT_BIND_LS_DOWN) {
          *thumb_ly = SHRT_MIN;
        }
        if (binding & XINPUT_BIND_LS_LEFT) {
          *thumb_lx = SHRT_MIN;
        }
        if (binding & XINPUT_BIND_LS_RIGHT) {
          *thumb_lx = SHRT_MAX;
        }

        if (binding & XINPUT_BIND_RS_UP) {
          *thumb_ry = SHRT_MAX;
        }
        if (binding & XINPUT_BIND_RS_DOWN) {
          *thumb_ry = SHRT_MIN;
        }
        if (binding & XINPUT_BIND_RS_LEFT) {
          *thumb_rx = SHRT_MIN;
        }
        if (binding & XINPUT_BIND_RS_RIGHT) {
          *thumb_rx = SHRT_MAX;
        }

        if (binding & XINPUT_BIND_MODIFIER) {
          *modifier_pressed = true;
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

}  // namespace hid
}  // namespace xe