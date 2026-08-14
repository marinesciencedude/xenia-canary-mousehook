/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define XENIA_MOUSEHOOK_MESSAGE
#ifndef XENIA_HID_WINKEY_HOOKABLE_GAME_H_
#define XENIA_HID_WINKEY_HOOKABLE_GAME_H_

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/hid/input.h"
#include "xenia/hid/input_driver.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"
#ifdef XENIA_MOUSEHOOK_MESSAGE
#include "xenia/ui/imgui_guest_notification.h"
#include "xenia/ui/imgui_host_notification.h"
#endif
namespace xe {
namespace hid {
namespace winkey {
static bool mousehook_message_read;
struct MouseEvent {
  int32_t x_delta = 0;
  int32_t y_delta = 0;
  int32_t buttons = 0;
  int32_t wheel_delta = 0;
};

struct RawInputState {
  MouseEvent mouse;
  bool* key_states;
};
enum MidHookStatus : BYTE {
  NOT_HOOKED,
  HOOKED,
};
class HookableGame {
 public:
  virtual ~HookableGame() = default;

  virtual bool IsGameSupported(GameVersion title_version) = 0;
  virtual bool DoHooks(uint32_t user_index, RawInputState& input_state,
                       X_INPUT_STATE* out_state) = 0;
  virtual std::string ChooseBinds() = 0;
  virtual bool ModifierKeyHandler(uint32_t user_index,
                                  RawInputState& input_state,
                                  X_INPUT_STATE* out_state) = 0;
  virtual void WeaponSwitchHandler(uint32_t user_index,
                                   RawInputState& input_state,
                                   X_INPUT_STATE* out_state, int weapon,
                                   uint16_t buttons) = 0;
  MidHookStatus midhook_status = NOT_HOOKED;
  virtual void MidHookInit() = 0;
};

xe::be<uint32_t>* multi_pointer(uint32_t base_address,
                                std::vector<uint32_t> offsets);

class guest_pattern_match {
 public:
  explicit guest_pattern_match(uint32_t guest_address)
      : guest_address_(guest_address) {}

  uint32_t guest_address(std::ptrdiff_t offset = 0) const {
    return static_cast<uint32_t>(static_cast<int64_t>(guest_address_) + offset);
  }

  template <typename T = void*>
  T translated(std::ptrdiff_t offset = 0) const {
    return xe::kernel::kernel_memory()->TranslateVirtual<T>(
        guest_address(offset));
  }

 private:
  uint32_t guest_address_;
};

class guest_pattern {
 public:
  explicit guest_pattern(std::string_view pattern);
  guest_pattern(std::string_view module_name, std::string_view pattern);
  guest_pattern(uint32_t guest_start, uint32_t guest_end,
                std::string_view pattern);

  guest_pattern&& count(uint32_t expected);
  size_t size();
  bool empty();

  guest_pattern_match get(size_t index);
  uint32_t get_first(std::ptrdiff_t offset = 0);

  template <typename T = void*>
  T get_first_translated(std::ptrdiff_t offset = 0) {
    return get(0).translated<T>(offset);
  }

 private:
  void Initialize(std::string_view pattern);
  void EnsureMatches(uint32_t max_count);

  uint32_t range_start_ = 0;
  uint32_t range_end_ = 0;
  std::vector<uint8_t> bytes_;
  std::vector<uint8_t> mask_;
  std::vector<guest_pattern_match> matches_;
  bool matched_ = false;
};

uint32_t get_guest_pattern(std::string_view pattern, std::ptrdiff_t offset = 0);

template <typename T = void*>
T get_guest_pattern_translated(std::string_view pattern,
                               std::ptrdiff_t offset = 0) {
  return guest_pattern(pattern).get_first_translated<T>(offset);
}

#ifdef XENIA_MOUSEHOOK_MESSAGE
enum notification_type : uint8_t {
  MESSAGE_TYPE_XNotify = 0,
  MESSAGE_TYPE_Host = 1
};
bool mousehook_message_wrapper(std::string message, bool handleseen = true,
                               notification_type type = MESSAGE_TYPE_Host,
                               uint8_t pos = 2);

#endif
}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_HOOKABLE_GAME_H_
