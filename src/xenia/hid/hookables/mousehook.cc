#include "xenia/hid/hookables/mousehook.h"

#include "xenia/base/system.h"

#include "xenia/hid/hookables/CallOfDuty.h"
#include "xenia/hid/hookables/Crackdown2.h"
#include "xenia/hid/hookables/DeadRising.h"
#include "xenia/hid/hookables/Farcry.h"
#include "xenia/hid/hookables/GearsOfWars.h"
#include "xenia/hid/hookables/JustCause.h"
#include "xenia/hid/hookables/Minecraft.h"
#include "xenia/hid/hookables/PerfectDarkZero.h"
#include "xenia/hid/hookables/RDR.h"
#include "xenia/hid/hookables/SaintsRow1.h"
#include "xenia/hid/hookables/SaintsRow2.h"
#include "xenia/hid/hookables/SourceEngine.h"
#include "xenia/hid/hookables/goldeneye.h"
#include "xenia/hid/hookables/halo3.h"

DECLARE_bool(swap_wheel);

namespace xe {
namespace hid {
using namespace xe::string_util;

static const std::unordered_map<std::string, uint64_t> kXInputButtons = {
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

    {"modifier", XINPUT_BIND_MODIFIER},

    {"weapon1", XINPUT_BIND_WEAPON1},
    {"weapon2", XINPUT_BIND_WEAPON2},
    {"weapon3", XINPUT_BIND_WEAPON3},
    {"weapon4", XINPUT_BIND_WEAPON4},
    {"weapon5", XINPUT_BIND_WEAPON5},
    {"weapon6", XINPUT_BIND_WEAPON6},
    {"weapon7", XINPUT_BIND_WEAPON7},
    {"weapon8", XINPUT_BIND_WEAPON8},
    {"weapon9", XINPUT_BIND_WEAPON9},
    {"weapon10", XINPUT_BIND_WEAPON10}};
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
    {"capslock", ui::VirtualKey::kCapital},

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

uint64_t ParseButtonCombination(const char* combo) {
  size_t len = strlen(combo);

  uint64_t retval = 0;
  std::string cur_token;

  // Parse combo tokens into buttons bitfield (tokens seperated by any
  // non-alphabetical char, eg. +)
  for (size_t i = 0; i < len; i++) {
    char c = combo[i];

    if (!isalpha(c) && !isdigit(c) && c != '-') {
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

void ParseCustomKeyBinding(
    std::map<uint32_t,
             std::map<std::string, std::map<ui::VirtualKey, uint64_t>>>&
        key_binds_) {
  auto path = std::filesystem::current_path() / "bindings.ini";
  const std::string_view bindings_file = path.string();

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

  std::map<ui::VirtualKey, uint64_t> cur_binds;
  std::map<std::string, std::map<ui::VirtualKey, uint64_t>> cur_title_binds;

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

void RegisterHookables(
    std::vector<std::unique_ptr<HookableGame>>& hookable_games_) {
  // Register our supported hookable games
  hookable_games_.push_back(std::move(std::make_unique<GoldeneyeGame>()));
  hookable_games_.push_back(std::move(std::make_unique<Halo3Game>()));
  hookable_games_.push_back(std::move(std::make_unique<SourceEngine>()));
  hookable_games_.push_back(std::move(std::make_unique<Crackdown2Game>()));
  hookable_games_.push_back(std::move(std::make_unique<SaintsRow2Game>()));
  hookable_games_.push_back(std::move(std::make_unique<SaintsRow1Game>()));
  hookable_games_.push_back(std::move(std::make_unique<JustCauseGame>()));
  hookable_games_.push_back(
      std::move(std::make_unique<RedDeadRedemptionGame>()));
  hookable_games_.push_back(std::move(std::make_unique<FarCryGame>()));
  hookable_games_.push_back(std::move(std::make_unique<GearsOfWarsGame>()));
  hookable_games_.push_back(std::move(std::make_unique<DeadRisingGame>()));
  hookable_games_.push_back(std::move(std::make_unique<CallOfDutyGame>()));
  hookable_games_.push_back(std::move(std::make_unique<PerfectDarkZeroGame>()));
  hookable_games_.push_back(std::move(std::make_unique<MinecraftGame>()));
}

void OnMouse(ui::MouseEvent& evt, std::queue<MouseEvent>& mouse_events_,
             uint8_t (&key_states_)[256]) {
  MouseEvent mouse;
  mouse.x_delta = evt.x();
  mouse.y_delta = evt.y();
  mouse.buttons = evt.scroll_x();
  mouse.wheel_delta = evt.scroll_y();
  mouse_events_.push(mouse);
#if XE_PLATFORM_WIN32
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
#endif
}

void HandleKeyBindings(
    RawInputState& state, std::queue<MouseEvent>& mouse_events_,
    uint8_t (&key_states_)[256],
    std::map<uint32_t,
             std::map<std::string, std::map<ui::VirtualKey, uint64_t>>>&
        key_binds_,
    uint32_t title_id,
    std::vector<std::unique_ptr<HookableGame>>& hookable_games_,
    uint16_t* buttons, uint8_t* left_trigger, uint8_t* right_trigger,
    int16_t* thumb_lx, int16_t* thumb_ly, int16_t* thumb_rx, int16_t* thumb_ry,
    bool* modifier_pressed, bool* weapon_switch, int* weapon) {
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
      std::map<ui::VirtualKey, uint64_t> binds;

      if (key_binds_.find(title_id) == key_binds_.end()) {
        binds = key_binds_.at(0).at("Default");
      } else {
        if (key_binds_.at(title_id).size() > 1) {
          bool contextual_binds = false;
          for (auto& game : hookable_games_) {
            if (game->IsGameSupported()) {
              binds = key_binds_.at(title_id).at(game->ChooseBinds());
              contextual_binds = true;
              break;
            }
          }
          if (!contextual_binds) {
            binds = key_binds_.at(title_id).at("Default");
          }
        } else {
          binds = key_binds_.at(title_id).at("Default");
        }
      }

      const auto vk_key = static_cast<ui::VirtualKey>(i);

      if (!binds.count(vk_key)) {
        continue;
      }

      const auto binding = binds.at(vk_key);

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

      if (binding & XINPUT_BIND_WEAPON1) {
        *weapon_switch = true;
        *weapon = 1;
      }
      if (binding & XINPUT_BIND_WEAPON2) {
        *weapon_switch = true;
        *weapon = 2;
      }
      if (binding & XINPUT_BIND_WEAPON3) {
        *weapon_switch = true;
        *weapon = 3;
      }
      if (binding & XINPUT_BIND_WEAPON4) {
        *weapon_switch = true;
        *weapon = 4;
      }
      if (binding & XINPUT_BIND_WEAPON5) {
        *weapon_switch = true;
        *weapon = 5;
      }
      if (binding & XINPUT_BIND_WEAPON6) {
        *weapon_switch = true;
        *weapon = 6;
      }
      if (binding & XINPUT_BIND_WEAPON7) {
        *weapon_switch = true;
        *weapon = 7;
      }
      if (binding & XINPUT_BIND_WEAPON8) {
        *weapon_switch = true;
        *weapon = 8;
      }
      if (binding & XINPUT_BIND_WEAPON9) {
        *weapon_switch = true;
        *weapon = 9;
      }
      if (binding & XINPUT_BIND_WEAPON10) {
        *weapon_switch = true;
        *weapon = 10;
      }
    }
  }
}

}  // namespace hid
}  // namespace xe