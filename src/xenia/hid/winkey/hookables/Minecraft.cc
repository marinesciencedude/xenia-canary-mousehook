/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/hookables/Minecraft.h"

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

namespace xe {
namespace hid {
namespace winkey {

MinecraftGame::~MinecraftGame() = default;

struct GameBuildAddrs {
  uint32_t camera_base_addr;
  std::string title_version;
  int32_t camera_x_offset;
  int32_t camera_y_offset;

  uint32_t inventory_flag;
  uint32_t inventory_ptr;

  uint32_t inventory_x_offset;
  uint32_t inventory_y_offset;
  uint32_t workbench_x_offset;
  uint32_t workbench_y_offset;
  uint32_t furnace_x_offset;
  uint32_t furnace_y_offset;
  uint32_t chest_x_offset; //chest (normal/trapped/ender), dispenser, dropper, hopper, minecart variants
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
};

//compiler cannot be trusted to std::map structs properly
struct GameBuildAddrs supported_builds[4] = {
    {NULL,  "",   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}, 
    {0x705AFD60, "1.0", 0x88, 0x8C, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL}, 
    { //TU68
        0x7018E75C, "1.0.73", 0x80, 0x84, 
        0x40AD7444, 0x40ED0140, 
        0x1A1C, 0x1A20, 
        0x14EC, 0x14F0,
        0x2118, 0x211C,
        0x1284, 0x1288,
        0x1A2C, 0x1A30,
        0x1D5C, 0x1D60,
        0x2390, 0x2394,
        0x1A48, 0x1A4C,
        0x25FC, 0x2600
    },
    { //TU75
        0x3002B02C, "1.0.80", -0x4EC, -0x4E8, 
        0x40A1B034, 0x409E3DC0, 
        0x1A1C, 0x1A20, 
        0x14EC, 0x14F0,
        0x2118, 0x211C,
        0x1284, 0x1288,
        0x1A2C, 0x1A30,
        0x1D5C, 0x1D60,
        0x2390, 0x2394,
        0x1A48, 0x1A4C,
        0x25FC, 0x2600
    }
};

/* std::map<MinecraftGame::GameBuild, GameBuildAddrs> supported_builds{
    {MinecraftGame::GameBuild::TU0, {0x705AFD60, "1.0",  0x88, 0x8C, NULL, NULL, NULL, NULL}},
    {MinecraftGame::GameBuild::TU68, {0x705AFD60, "1.0.73",  }},
    {MinecraftGame::GameBuild::TU75,{0x3002B02C, "1.0.80", -0x4EC, -0x4E8, 0x409EC558, 0x409E3DC0, 0x1A1C, 0x1A20}}};*/

bool MinecraftGame::IsGameSupported() {
  auto title_id = kernel_state()->title_id();
  if (title_id != 0x584111F7)
    return false;

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  /* for (auto& build : supported_builds) {
    if (current_version == build.second.title_version) {
      game_build_ = build.first;
      return true;
    }
  }*/
  for (int i = 0; i < sizeof(supported_builds); i++)
  {
        if (current_version == supported_builds[i].title_version)
        {
            game_build_ = static_cast<MinecraftGame::GameBuild>(i);
            return true;
        }
  }

  return false;
}

bool MinecraftGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                           X_INPUT_STATE* out_state) {
    if (!IsGameSupported()) {
    return false;
    }

    XThread* current_thread = XThread::GetCurrentThread();

    if (!current_thread) {
    return false;
    }

    uint32_t inv_flag = supported_builds[game_build_].inventory_flag;
    if (inv_flag)
    {
        auto inventory_flag_ptr = *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(supported_builds[game_build_].inventory_flag);

        if (inventory_flag_ptr && inventory_flag_ptr > 0x0000000040000000) 
        {
            auto inventory_flag = *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(inventory_flag_ptr + 0x4);
            if (inventory_flag)
            {
                uint32_t x_offset;
                uint32_t y_offset;
    
                switch (inventory_flag) 
                {
                case 1:
                    {
                        x_offset = supported_builds[game_build_].inventory_x_offset;
                        y_offset = supported_builds[game_build_].inventory_y_offset;
                        break;
                    }
                case 37:
                    {
                        x_offset = supported_builds[game_build_].workbench_x_offset;
                        y_offset = supported_builds[game_build_].workbench_y_offset;
                        break;
                    }
                case 4: 
                    {
                        x_offset = supported_builds[game_build_].furnace_x_offset;        
                        y_offset = supported_builds[game_build_].furnace_y_offset;
                        break;
                    }
                case 10: //normal/trapped/ender chests
                case 11: //dispenser/dropper
                case 32: //hopper
                    {
                        x_offset = supported_builds[game_build_].chest_x_offset;
                        y_offset = supported_builds[game_build_].chest_y_offset;
                        break;
                    }
                case 27:
                    {
                        x_offset = supported_builds[game_build_].anvil_x_offset;
                        y_offset = supported_builds[game_build_].anvil_y_offset;
                        break;
                    }
                case 20: 
                    {
                        x_offset = supported_builds[game_build_].enchanting_x_offset;
                        y_offset = supported_builds[game_build_].enchanting_y_offset;
                        break;
                    }
                case 18: 
                    {
                        x_offset = supported_builds[game_build_].brewing_x_offset;
                        y_offset = supported_builds[game_build_].brewing_y_offset;
                        break;
                    }
                case 34: 
                    {
                        x_offset = supported_builds[game_build_].beacon_x_offset;
                        y_offset = supported_builds[game_build_].beacon_y_offset;
                        break;
                    }
                case 14: 
                    {
                        x_offset = supported_builds[game_build_].creative_x_offset;
                        y_offset = supported_builds[game_build_].creative_y_offset;
                        break;
                    }
                default: //sometimes we need to check if offsets are being set at all to make sure it doesn't crash when re-entering games
                     return false;
                }

                auto inventory_addr = *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(supported_builds[game_build_].inventory_ptr);
                if (inventory_addr != 0)
                {
                    auto inventory_input = *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(inventory_addr);
                    auto* inventory_ptr = kernel_memory()->TranslateVirtual(inventory_input);

                    auto* inventoryX_ptr = reinterpret_cast<xe::be<float>*>(inventory_ptr + x_offset);
                    auto* inventoryY_ptr = reinterpret_cast<xe::be<float>*>(inventory_ptr + y_offset);

                    float inventoryX = *inventoryX_ptr;
                    float inventoryY = *inventoryY_ptr;

                    inventoryX += (((float)input_state.mouse.x_delta)) *
                                (float)cvars::sensitivity;

                    inventoryY += (((float)input_state.mouse.y_delta)) *
                                (float)cvars::sensitivity;

                    *inventoryX_ptr = inventoryX;
                    *inventoryY_ptr = inventoryY;

                    return true;
                }
            }
        }
    }

    auto global_addr = *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(supported_builds[game_build_].camera_base_addr);
    if (global_addr && 0x0000000040000000 < global_addr && global_addr < 0x0000000050000000) { //is realistically going to be between 40000000-50000000
      auto* input_globals = kernel_memory()->TranslateVirtual(global_addr);

      auto* player_cam_x = reinterpret_cast<xe::be<float>*>(input_globals + supported_builds[game_build_].camera_x_offset);
      auto* player_cam_y = reinterpret_cast<xe::be<float>*>(input_globals + supported_builds[game_build_].camera_y_offset);

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

      //Keep in bounds because game can't catch up
      if (camY > 90.0f)
          camY = 90.0f;
      else if (camY < -90.0f) 
          camY = -90.0f;

      *player_cam_x = camX;
      *player_cam_y = camY;
    }

    return true;
}

bool MinecraftGame::ModifierKeyHandler(uint32_t user_index,
                                      RawInputState& input_state,
                                      X_INPUT_STATE* out_state) {
  float thumb_lx = (int16_t)out_state->gamepad.thumb_lx;
  float thumb_ly = (int16_t)out_state->gamepad.thumb_ly;
  return false;
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
