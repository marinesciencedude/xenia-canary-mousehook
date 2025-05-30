/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/hookables/Minecraft.h"

#include "xenia/emulator.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"

using namespace xe::kernel;

DECLARE_double(sensitivity);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);

namespace xe {
namespace hid {
namespace winkey {

MinecraftGame::~MinecraftGame() = default;

struct GameBuildAddrs {
  const char* title_version;
  uint32_t camera_base_addr;
  std::vector<uint32_t> camera_offsets;
  uint32_t camera_x_offset;
  uint32_t camera_y_offset;

  uint32_t pause_flag;

  uint32_t inventory_flag_base;
  std::vector<uint32_t> inventory_flag_offsets;

  uint32_t inventory_base_addr;
  std::vector<uint32_t> inventory_base_offsets;

  uint32_t inventory_x_offset;
  uint32_t inventory_y_offset;
  uint32_t workbench_x_offset;
  uint32_t workbench_y_offset;
  uint32_t furnace_x_offset;
  uint32_t furnace_y_offset;
  uint32_t chest_x_offset;  // chest (normal/trapped/ender), dispenser, dropper,
                            // hopper, minecart variants
  uint32_t chest_y_offset;
  uint32_t anvil_x_offset;
  uint32_t anvil_y_offset;
  uint32_t enchanting_x_offset;
  uint32_t enchanting_y_offset;
  uint32_t brewing_x_offset;
  uint32_t brewing_y_offset;
  uint32_t beacon_x_offset;
  uint32_t beacon_y_offset;
  uint32_t creative_x_offset;
  uint32_t creative_y_offset;

  uint32_t hotbar_base_addr;
  std::vector<uint32_t> hotbar_offsets;
};

std::map<MinecraftGame::GameBuild, GameBuildAddrs> supported_builds{
    {MinecraftGame::GameBuild::Unknown,
     {"",   NULL, {},   NULL, NULL, NULL, NULL, {},   NULL, {},
      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, {}}},
    {MinecraftGame::GameBuild::TU75, {"0.0.80.1", 0x82C8518C,
                                      {0x38},     0x148,
                                      0x14C,      0x82C84986,
                                      0x82C83084, {0x24, 0x114, 0x4},
                                      0x82CCDB90, {0x4, 0x44, 0x6B8},
                                      0x1A1C,     0x1A20,
                                      0x14EC,     0x14F0,
                                      0x2118,     0x211C,
                                      0x1284,     0x1288,
                                      0x1A2C,     0x1A30,
                                      0x1D5C,     0x1D60,
                                      0x2390,     0x2394,
                                      0x1A48,     0x1A4C,
                                      0x25FC,     0x2600,
                                      0x82CCDB90, {0x34, 0x5F8, 0x6C}}}};
std::map<std::string, GameVersion> supported_versions{
    {"", {NULL, NULL, NULL, NULL}}, {"0.0.80.1", {0, 0, 80, 1}}};

bool MinecraftGame::IsGameSupported(GameVersion title_version) {
  auto title_id = kernel_state()->title_id();
  if (title_id != 0x584111F7) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    GameVersion build_version =
        supported_versions.at(build.second.title_version);
    if (std::memcmp(&title_version, &build_version, sizeof(GameVersion)) == 0) {
      game_build_ = build.first;
      return true;
    }
  }

#ifdef XENIA_MOUSEHOOK_MESSAGE
  mousehook_message_wrapper(
      std::format("MOUSEHOOK: Supported Title ID, but current version '{}' is "
                  "unsupported. Expected: [{}]",
                  current_version,
                  [&]() {
                    std::string versions;
                    for (const auto& build : supported_builds) {
                      if (!versions.empty()) {
                        versions += ", ";
                      }
                      versions += build.second.title_version;
                    }
                    return versions;
                  }()),
      true, MESSAGE_TYPE_XNotify);
#endif

  return false;
}

bool MinecraftGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                            X_INPUT_STATE* out_state) {
  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }

  if (*kernel_memory()->TranslateVirtual<uint8_t*>(
          supported_builds[game_build_].pause_flag)) {
    return true;
  }

  auto* inventory_flag_ptr =
      multi_pointer(supported_builds[game_build_].inventory_flag_base,
                    supported_builds[game_build_].inventory_flag_offsets);
  if (inventory_flag_ptr) {
    if (*inventory_flag_ptr) {
      uint32_t x_offset;
      uint32_t y_offset;

      switch (*inventory_flag_ptr) {
        case 1: {
          x_offset = supported_builds[game_build_].inventory_x_offset;
          y_offset = supported_builds[game_build_].inventory_y_offset;
          break;
        }
        case 37: {
          x_offset = supported_builds[game_build_].workbench_x_offset;
          y_offset = supported_builds[game_build_].workbench_y_offset;
          break;
        }
        case 4: {
          x_offset = supported_builds[game_build_].furnace_x_offset;
          y_offset = supported_builds[game_build_].furnace_y_offset;
          break;
        }
        case 10:  // normal/trapped/ender chests
        case 11:  // dispenser/dropper
        case 32:  // hopper
        {
          x_offset = supported_builds[game_build_].chest_x_offset;
          y_offset = supported_builds[game_build_].chest_y_offset;
          break;
        }
        case 27: {
          x_offset = supported_builds[game_build_].anvil_x_offset;
          y_offset = supported_builds[game_build_].anvil_y_offset;
          break;
        }
        case 20: {
          x_offset = supported_builds[game_build_].enchanting_x_offset;
          y_offset = supported_builds[game_build_].enchanting_y_offset;
          break;
        }
        case 18: {
          x_offset = supported_builds[game_build_].brewing_x_offset;
          y_offset = supported_builds[game_build_].brewing_y_offset;
          break;
        }
        case 34: {
          x_offset = supported_builds[game_build_].beacon_x_offset;
          y_offset = supported_builds[game_build_].beacon_y_offset;
          break;
        }
        case 14: {
          x_offset = supported_builds[game_build_].creative_x_offset;
          y_offset = supported_builds[game_build_].creative_y_offset;
          break;
        }
        default:  // sometimes we need to check if offsets are being set at all
                  // to make sure it doesn't crash when re-entering games
          return false;
      }

      auto* inventory_ptr =
          multi_pointer(supported_builds[game_build_].inventory_base_addr,
                        supported_builds[game_build_].inventory_base_offsets);
      if (*inventory_ptr) {
        auto* inventoryX_ptr =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(*inventory_ptr +
                                                              x_offset);
        auto* inventoryY_ptr =
            kernel_memory()->TranslateVirtual<xe::be<float>*>(*inventory_ptr +
                                                              y_offset);

        float inventoryX = *inventoryX_ptr;
        float inventoryY = *inventoryY_ptr;

        inventoryX +=
            (((float)input_state.mouse.x_delta)) * (float)cvars::sensitivity;

        inventoryY +=
            (((float)input_state.mouse.y_delta)) * (float)cvars::sensitivity;

        *inventoryX_ptr = inventoryX;
        *inventoryY_ptr = inventoryY;

        return true;
      }
    }
  }

  auto* input_base_addr =
      multi_pointer(supported_builds[game_build_].camera_base_addr,
                    supported_builds[game_build_].camera_offsets);

  if (*input_base_addr) {
    auto* player_cam_x = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        *input_base_addr + supported_builds[game_build_].camera_x_offset);
    auto* player_cam_y = kernel_memory()->TranslateVirtual<xe::be<float>*>(
        *input_base_addr + supported_builds[game_build_].camera_y_offset);

    // Have to do weird things converting it to normal float otherwise
    // xe::be += treats things as int?
    float camX = (float)*player_cam_x;
    float camY = (float)*player_cam_y;

    if (!cvars::invert_x) {
      camX += (((float)input_state.mouse.x_delta) / 5.f) *
              (float)cvars::sensitivity;
    } else {
      camX -= (((float)input_state.mouse.x_delta) / 5.f) *
              (float)cvars::sensitivity;
    }

    if (!cvars::invert_y) {
      camY += (((float)input_state.mouse.y_delta) / 5.f) *
              (float)cvars::sensitivity;
    } else {
      camY -= (((float)input_state.mouse.y_delta) / 5.f) *
              (float)cvars::sensitivity;
    }

    // Keep in bounds because game can't catch up
    if (camY > 90.0f) {
      camY = 90.0f;
    } else if (camY < -90.0f) {
      camY = -90.0f;
    }

    *player_cam_x = camX;
    *player_cam_y = camY;

    return true;
  }

  return false;
}

std::string MinecraftGame::ChooseBinds() {
  auto* inventory_flag_ptr =
      multi_pointer(supported_builds[game_build_].inventory_flag_base,
                    supported_builds[game_build_].inventory_flag_offsets);
  if (inventory_flag_ptr) {
    if (*inventory_flag_ptr) {
      return "Inventory";
    }
  }

  return "Default";
}

bool MinecraftGame::ModifierKeyHandler(uint32_t user_index,
                                       RawInputState& input_state,
                                       X_INPUT_STATE* out_state) {
  return false;
}

void MinecraftGame::WeaponSwitchHandler(uint32_t user_index,
                                        RawInputState& input_state,
                                        X_INPUT_STATE* out_state, int weapon,
                                        uint16_t buttons) {
  auto* hotbar_selection =
      multi_pointer(supported_builds[game_build_].hotbar_base_addr,
                    supported_builds[game_build_].hotbar_offsets);
  if (hotbar_selection) {
    if (weapon == 1) {
      *hotbar_selection = 0;
    } else if (weapon == 2) {
      *hotbar_selection = 1;
    } else if (weapon == 3) {
      *hotbar_selection = 2;
    } else if (weapon == 4) {
      *hotbar_selection = 3;
    } else if (weapon == 5) {
      *hotbar_selection = 4;
    } else if (weapon == 6) {
      *hotbar_selection = 5;
    } else if (weapon == 7) {
      *hotbar_selection = 6;
    } else if (weapon == 8) {
      *hotbar_selection = 7;
    } else if (weapon == 9) {
      *hotbar_selection = 8;
    }
  }
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
