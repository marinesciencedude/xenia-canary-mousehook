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
#include "xenia/hid/input_driver.h"
#include "xenia/hid/winkey/hookables/hookable_game.h"
#include "xenia/ui/virtual_key.h"

#define VK_BIND_MWHEELUP 0x0E
#define VK_BIND_MWHEELDOWN 0x0F

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

  ui::VirtualKey ParseButtonCombination(const char* combo);

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
  std::map<uint32_t, std::map<ui::VirtualKey, ui::VirtualKey>> key_binds_;

  uint32_t packet_number_ = 1;

  std::vector<std::unique_ptr<HookableGame>> hookable_games_;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_WINKEY_INPUT_DRIVER_H_
