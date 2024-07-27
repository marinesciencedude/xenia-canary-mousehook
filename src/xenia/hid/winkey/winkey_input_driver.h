/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_WINKEY_INPUT_DRIVER_H_
#define XENIA_HID_WINKEY_WINKEY_INPUT_DRIVER_H_

#include <queue>

#include "xenia/base/mutex.h"
#include "xenia/hid/input.h"
#include "xenia/hid/input_driver.h"
#include "xenia/hid/winkey/hookables/hookable_game.h"
#include "xenia/ui/virtual_key.h"

#define VK_BIND_MWHEELUP 0x0E
#define VK_BIND_MWHEELDOWN 0x0F

#define XINPUT_BUTTONS_MASK 0xFFFF

#define XINPUT_BIND_UP X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_DPAD_UP
#define XINPUT_BIND_DOWN X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_DPAD_DOWN
#define XINPUT_BIND_LEFT X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_DPAD_LEFT
#define XINPUT_BIND_RIGHT X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_DPAD_RIGHT

#define XINPUT_BIND_START X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_START
#define XINPUT_BIND_BACK X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_BACK

#define XINPUT_BIND_LS X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_LEFT_THUMB
#define XINPUT_BIND_RS X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_RIGHT_THUMB

#define XINPUT_BIND_LB X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_LEFT_SHOULDER
#define XINPUT_BIND_RB X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_RIGHT_SHOULDER

#define XINPUT_BIND_A X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_A
#define XINPUT_BIND_B X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_B
#define XINPUT_BIND_X X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_X
#define XINPUT_BIND_Y X_INPUT_GAMEPAD_BUTTON::X_INPUT_GAMEPAD_Y

#define XINPUT_BIND_LEFT_TRIGGER (1 << 16)
#define XINPUT_BIND_RIGHT_TRIGGER (1 << 17)

#define XINPUT_BIND_LS_UP (1 << 18)
#define XINPUT_BIND_LS_DOWN (1 << 19)
#define XINPUT_BIND_LS_LEFT (1 << 20)
#define XINPUT_BIND_LS_RIGHT (1 << 21)

#define XINPUT_BIND_RS_UP (1 << 22)
#define XINPUT_BIND_RS_DOWN (1 << 23)
#define XINPUT_BIND_RS_LEFT (1 << 24)
#define XINPUT_BIND_RS_RIGHT (1 << 25)

#define XINPUT_BIND_MODIFIER (1 << 26)

namespace xe {
namespace hid {
namespace winkey {

class WinKeyInputDriver final : public InputDriver {
 public:
  explicit WinKeyInputDriver(xe::ui::Window* window, size_t window_z_order);
  ~WinKeyInputDriver() override;

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

 protected:
  struct KeyEvent {
    ui::VirtualKey virtual_key = ui::VirtualKey::kNone;
    int repeat_count = 0;
    bool transition = false;  // going up(false) or going down(true)
    bool prev_state = false;  // down(true) or up(false)
  };

  struct KeyBinding {
    ui::VirtualKey input_key = ui::VirtualKey::kNone;
    ui::VirtualKey output_key = ui::VirtualKey::kNone;
    bool uppercase = false;
    bool lowercase = false;
  };

  class WinKeyWindowInputListener final : public ui::WindowInputListener {
   public:
    explicit WinKeyWindowInputListener(WinKeyInputDriver& driver)
        : driver_(driver) {}

    void OnKeyDown(ui::KeyEvent& e) override;
    void OnKeyUp(ui::KeyEvent& e) override;
    void OnRawKeyboard(ui::KeyEvent& e) override;
    void OnRawMouse(ui::MouseEvent& e) override;

   private:
    WinKeyInputDriver& driver_;
  };

  void ParseKeyBinding(ui::VirtualKey virtual_key,
                       const std::string_view description,
                       const std::string_view binding);

  int ParseButtonCombination(const char* combo);

  void ParseCustomKeyBinding(const std::string_view bindings_file);

  void OnRawKeyboard(ui::KeyEvent& e);

  void OnKey(ui::KeyEvent& e, bool is_down);

  void OnRawMouse(ui::MouseEvent& e);

  WinKeyWindowInputListener window_input_listener_;

  xe::global_critical_region global_critical_region_;
  std::queue<KeyEvent> key_events_;
  std::vector<KeyBinding> key_bindings_;

  std::queue<MouseEvent> mouse_events_;

  uint8_t key_states_[256] = {};
  std::map<uint32_t, std::map<std::string, std::map<ui::VirtualKey, uint32_t>>>
      key_binds_;

  uint32_t packet_number_ = 1;

  std::vector<std::unique_ptr<HookableGame>> hookable_games_;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_WINKEY_INPUT_DRIVER_H_
