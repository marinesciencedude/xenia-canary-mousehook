/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/hookables/hookable_game.h"

#include <atomic>

#include "xenia/emulator.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace hid {
namespace winkey {

xe::be<uint32_t>* multi_pointer(uint32_t base_address,
                                std::vector<uint32_t> offsets) {
  auto* current_pointer =
      xe::kernel::kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
          base_address);
  for (auto& offset : offsets) {
    if (*current_pointer) {
      current_pointer =
          xe::kernel::kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
              *current_pointer + offset);
    } else {
      return nullptr;
    }
  }
  return current_pointer;
}

static std::atomic<int64_t> raw_mouse_x_{0};
static std::atomic<int64_t> raw_mouse_y_{0};

void AddRawMouseMotion(int32_t x_delta, int32_t y_delta) {
  raw_mouse_x_.fetch_add(x_delta, std::memory_order_relaxed);
  raw_mouse_y_.fetch_add(y_delta, std::memory_order_relaxed);
}

void TakeRawMouseMotion(int64_t& x_delta, int64_t& y_delta) {
  x_delta = raw_mouse_x_.exchange(0, std::memory_order_relaxed);
  y_delta = raw_mouse_y_.exchange(0, std::memory_order_relaxed);
}
#ifdef XENIA_MOUSEHOOK_MESSAGE
bool mousehook_message_wrapper(std::string message, bool handleseen,
                               notification_type type, uint8_t pos) {
  if (mousehook_message_read && handleseen) {
    return false;
  }
  XELOGE(message);
  const Emulator* emulator = xe::kernel::kernel_state()->emulator();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  if (type == MESSAGE_TYPE_XNotify) {
    new xe::ui::XNotifyWindow(imgui_drawer, "", message, 0, 2);
  }
  if (type == MESSAGE_TYPE_Host) {
    ui::WindowedAppContext& app_context =
        emulator->display_window()->app_context();
    app_context.CallInUIThread([imgui_drawer, message, pos]() {
      new xe::ui::HostNotificationWindow(imgui_drawer, "ERROR", message, 0,
                                         pos);
    });
  }
  if (!mousehook_message_read && handleseen) {
    mousehook_message_read = true;
  }

  return true;
}
#endif
}  // namespace winkey
}  // namespace hid
}  // namespace xe
