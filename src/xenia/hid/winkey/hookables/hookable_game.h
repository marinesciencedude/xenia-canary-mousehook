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

#include <vector>
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/hid/input.h"
#include "xenia/hid/input_driver.h"
#include "xenia/xbox.h"
#ifdef XENIA_MOUSEHOOK_MESSAGE
#include "xenia/ui/imgui_guest_notification.h"
#include "xenia/ui/imgui_host_notification.h"
#endif
namespace xe {
namespace cpu {
namespace ppc {
using MouseHookMidHook = void (*)(PPCContext* context, void* arg0, void* arg1);
extern void RegisterMidHookASM(uint32_t address,
                               MouseHookMidHook hook_function);
};  // namespace ppc
};  // namespace cpu
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
