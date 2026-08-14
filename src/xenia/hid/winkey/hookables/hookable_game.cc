/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/hookables/hookable_game.h"

#include <cctype>
#include <limits>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/emulator.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"

namespace xe {
namespace hid {
namespace winkey {
namespace {

uint8_t HexDigit(char ch) {
  if (ch >= '0' && ch <= '9') {
    return static_cast<uint8_t>(ch - '0');
  }
  if (ch >= 'A' && ch <= 'F') {
    return static_cast<uint8_t>(ch - 'A' + 10);
  }
  if (ch >= 'a' && ch <= 'f') {
    return static_cast<uint8_t>(ch - 'a' + 10);
  }
  return 0;
}

bool IsHexDigit(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
         (ch >= 'a' && ch <= 'f');
}

bool ResolveModuleRange(std::string_view module_name, uint32_t* out_start,
                        uint32_t* out_end) {
  kernel::UserModule* user_module = nullptr;

  if (module_name.empty()) {
    auto executable_module = kernel::kernel_state()->GetExecutableModule();
    user_module = executable_module.get();
  } else {
    auto module = kernel::kernel_state()->GetModule(module_name, true);
    if (module &&
        module->module_type() == kernel::XModule::ModuleType::kUserModule) {
      user_module = module.get<kernel::UserModule>();
    }
  }

  if (!user_module) {
    return false;
  }

  auto* xex_module = user_module->xex_module();
  if (!xex_module) {
    return false;
  }

  uint32_t base = xex_module->base_address();
  uint32_t size = xex_module->image_size();
  if (!base || !size || std::numeric_limits<uint32_t>::max() - base < size) {
    return false;
  }

  *out_start = base;
  *out_end = base + size;
  return true;
}

}  // namespace

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

guest_pattern::guest_pattern(std::string_view pattern) {
  ResolveModuleRange({}, &range_start_, &range_end_);
  Initialize(pattern);
}

guest_pattern::guest_pattern(std::string_view module_name,
                             std::string_view pattern) {
  ResolveModuleRange(module_name, &range_start_, &range_end_);
  Initialize(pattern);
}

guest_pattern::guest_pattern(uint32_t guest_start, uint32_t guest_end,
                             std::string_view pattern)
    : range_start_(guest_start), range_end_(guest_end) {
  Initialize(pattern);
}

void guest_pattern::Initialize(std::string_view pattern) {
  bytes_.clear();
  mask_.clear();
  matches_.clear();
  matched_ = false;

  for (size_t i = 0; i < pattern.size();) {
    char ch = pattern[i];
    if (std::isspace(static_cast<unsigned char>(ch))) {
      ++i;
      continue;
    }

    if (ch == '?') {
      bytes_.push_back(0);
      mask_.push_back(0);
      ++i;
      if (i < pattern.size() && pattern[i] == '?') {
        ++i;
      }
      continue;
    }

    if (!IsHexDigit(ch)) {
      ++i;
      continue;
    }

    uint8_t value = static_cast<uint8_t>(HexDigit(ch) << 4);
    ++i;
    if (i < pattern.size() && IsHexDigit(pattern[i])) {
      value |= HexDigit(pattern[i]);
      ++i;
    }
    bytes_.push_back(value);
    mask_.push_back(0xFF);
  }
}

void guest_pattern::EnsureMatches(uint32_t max_count) {
  if (matched_ || !range_start_ || !range_end_ || bytes_.empty() ||
      bytes_.size() != mask_.size() || range_end_ <= range_start_ ||
      range_end_ - range_start_ < bytes_.size()) {
    matched_ = true;
    return;
  }

  auto* memory = kernel::kernel_memory();
  uint8_t* scan_base = memory->TranslateVirtual(range_start_);
  size_t scan_size = range_end_ - range_start_;
  size_t pattern_size = bytes_.size();

  for (size_t offset = 0; offset <= scan_size - pattern_size; ++offset) {
    bool found = true;
    for (size_t i = 0; i < pattern_size; ++i) {
      if (mask_[i] && scan_base[offset + i] != bytes_[i]) {
        found = false;
        break;
      }
    }

    if (!found) {
      continue;
    }

    matches_.emplace_back(range_start_ + static_cast<uint32_t>(offset));
    if (matches_.size() == max_count) {
      break;
    }
  }

  matched_ = true;
}

guest_pattern&& guest_pattern::count(uint32_t expected) {
  uint32_t max_count = expected == std::numeric_limits<uint32_t>::max()
                           ? expected
                           : expected + 1;
  EnsureMatches(max_count);
  assert_true(matches_.size() == expected);
  return std::move(*this);
}

size_t guest_pattern::size() {
  EnsureMatches(std::numeric_limits<uint32_t>::max());
  return matches_.size();
}

bool guest_pattern::empty() { return size() == 0; }

guest_pattern_match guest_pattern::get(size_t index) {
  EnsureMatches(std::numeric_limits<uint32_t>::max());
  assert_true(index < matches_.size());
  return matches_[index];
}

uint32_t guest_pattern::get_first(std::ptrdiff_t offset) {
  return get(0).guest_address(offset);
}

uint32_t get_guest_pattern(std::string_view pattern, std::ptrdiff_t offset) {
  return guest_pattern(pattern).get_first(offset);
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
