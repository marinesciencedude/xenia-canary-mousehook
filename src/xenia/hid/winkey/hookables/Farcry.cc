/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/Farcry.h"

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

const uint32_t kTitleIdFarCry = 0x555307DC;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* title_version;
  uint32_t base_address;
  uint32_t x_offset;
  uint32_t y_offset;
};

std::map<FarCryGame::GameBuild, GameBuildAddrs> supported_builds{
    {FarCryGame::GameBuild::FarCry_TU0, {"0.0.0.1", 0x829138B8, 0x3AC, 0x3A4}}};
std::map<std::string, GameVersion> supported_farcry_versions{
    {"", {NULL, NULL, NULL, NULL}}, {"0.0.0.1", {0, 0, 0, 1}}};

FarCryGame::~FarCryGame() = default;

bool FarCryGame::IsGameSupported(GameVersion title_version) {
  if (kernel_state()->title_id() != kTitleIdFarCry) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    GameVersion build_version =
        supported_farcry_versions.at(build.second.title_version);
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
      true);
#endif

  return false;
}

bool FarCryGame::DoHooks(uint32_t user_index, RawInputState& input_state,
                         X_INPUT_STATE* out_state) {
  if (supported_builds.count(game_build_) == 0) {
    return false;
  }

  if ((!input_state.mouse.x_delta && !input_state.mouse.y_delta &&
       !input_state.mouse.wheel_delta)) {
    return false;
  }

  xe::be<uint32_t>* base_address =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
          supported_builds[game_build_].base_address);

  if (!base_address || *base_address == NULL) {
    // Not in game
    return false;
  }

  xe::be<uint32_t> x_address =
      *base_address + supported_builds[game_build_].x_offset;
  xe::be<uint32_t> y_address =
      *base_address + supported_builds[game_build_].y_offset;

  xe::be<float>* degree_x =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(x_address);

  xe::be<float>* degree_y =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(y_address);

  float new_degree_x = *degree_x;
  float new_degree_y = *degree_y;

  if (!cvars::invert_x) {
    new_degree_x -=
        (input_state.mouse.x_delta / 7.5f) * (float)cvars::sensitivity;
  } else {
    new_degree_x +=
        (input_state.mouse.x_delta / 7.5f) * (float)cvars::sensitivity;
  }
  *degree_x = new_degree_x;

  if (!cvars::invert_y) {
    new_degree_y +=
        (input_state.mouse.y_delta / 7.5f) * (float)cvars::sensitivity;
  } else {
    new_degree_y -=
        (input_state.mouse.y_delta / 7.5f) * (float)cvars::sensitivity;
  }
  *degree_y = new_degree_y;

  return true;
}

std::string FarCryGame::ChooseBinds() { return "Default"; }

bool FarCryGame::ModifierKeyHandler(uint32_t user_index,
                                    RawInputState& input_state,
                                    X_INPUT_STATE* out_state) {
  return false;
}

void FarCryGame::WeaponSwitchHandler(uint32_t user_index,
                                     RawInputState& input_state,
                                     X_INPUT_STATE* out_state, int weapon,
                                     uint16_t buttons) {}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
