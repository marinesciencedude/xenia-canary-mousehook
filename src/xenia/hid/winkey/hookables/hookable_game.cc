/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/hookables/hookable_game.h"
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

}  // namespace winkey
}  // namespace hid
}  // namespace xe
