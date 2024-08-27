/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/GearsOfWars.h"

#include "xenia/base/platform_win.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

using namespace xe::kernel;

DECLARE_double(sensitivity);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);

const uint32_t kTitleIdGearsOfWars2 = 0x4D53082D;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t camera_base_address;
  uint32_t x_offset;
  uint32_t y_offset;
  uint32_t menu_status_address;
};

std::map<GearsOfWarsGame::GameBuild, GameBuildAddrs> supported_builds{
    {GearsOfWarsGame::GameBuild::Unknown, {"", NULL, NULL, NULL}},
    {GearsOfWarsGame::GameBuild::GearsOfWars2_TU6,
     {"5.0.6", 0x40874800, 0x66, 0x62, 0x83146F3F}}};

GearsOfWarsGame::~GearsOfWarsGame() = default;

bool GearsOfWarsGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdGearsOfWars2) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    if (current_version == build.second.title_version) {
      game_build_ = build.first;
      return true;
    }
  }

  return false;
}

bool GearsOfWarsGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                              X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  if (supported_builds.count(game_build_) == 0) {
    return false;
  }

  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }
  auto* menu_status = kernel_memory()->TranslateVirtual<uint8_t*>(
      supported_builds[game_build_].menu_status_address);
  static bool quickworkaround = false;
  if (quickworkaround || menu_status && *menu_status == 2) {
    quickworkaround = true;
    xe::be<uint16_t>* degree_x;
    xe::be<uint16_t>* degree_y;
    uint32_t base_address =
        *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
            supported_builds[game_build_].camera_base_address);

    degree_x = kernel_memory()->TranslateVirtual<xe::be<uint16_t>*>(
        base_address + 0x66);

    degree_y = kernel_memory()->TranslateVirtual<xe::be<uint16_t>*>(
        base_address + 0x62);

    uint16_t x_delta = static_cast<uint16_t>((input_state.mouse.x_delta * 10) *
                                             cvars::sensitivity);
    uint16_t y_delta = static_cast<uint16_t>((input_state.mouse.y_delta * 10) *
                                             cvars::sensitivity);
    if (!cvars::invert_x) {
      *degree_x += x_delta;
    } else {
      *degree_x -= x_delta;
    }

    if (!cvars::invert_y) {
      *degree_y -= y_delta;
    } else {
      *degree_y += y_delta;
    }
  }
  return true;
}

std::string GearsOfWarsGame::ChooseBinds() { return "Default"; }

bool GearsOfWarsGame::ModifierKeyHandler(uint32_t user_index,
                                         RawInputState& input_state,
                                         X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe