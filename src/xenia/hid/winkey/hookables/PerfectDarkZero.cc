/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/PerfectDarkZero.h"

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

const uint32_t kTitleIdPerfectDarkZero = 0x4D5307D3;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  const char* build_string;
  uint32_t build_string_addr;
  uint32_t base_address;
  uint32_t x_offset;
  uint32_t y_offset;
};

std::map<PerfectDarkZeroGame::GameBuild, GameBuildAddrs> supported_builds{
    {PerfectDarkZeroGame::GameBuild::PerfectDarkZero_TU0,
     {"CLIENT.Ph.Rare-PerfectDarkZero", 0x820BD7A4, 0x82D2AD38, 0x150,
      0x1674}}};

PerfectDarkZeroGame::~PerfectDarkZeroGame() = default;

bool PerfectDarkZeroGame::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdPerfectDarkZero) {
    return false;
  }

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    auto* build_ptr = kernel_memory()->TranslateVirtual<const char*>(
        build.second.build_string_addr);

    if (strcmp(build_ptr, build.second.build_string) == 0) {
      game_build_ = build.first;
      return true;
    }
  }

  return false;
}

float PerfectDarkZeroGame::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float PerfectDarkZeroGame::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}

bool PerfectDarkZeroGame::DoHooks(uint32_t user_index,
                                  RawInputState& input_state,
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

  xe::be<uint32_t>* base_address =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
          supported_builds[game_build_].base_address);

  xe::be<uint32_t>* radians_x_base =
      kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(*base_address +
                                                           0xE4);

  if (!base_address || *base_address == NULL) {
    // Not in game
    return false;
  }

  xe::be<uint32_t> x_address =
      *radians_x_base + supported_builds[game_build_].x_offset;
  xe::be<uint32_t> y_address =
      *base_address + supported_builds[game_build_].y_offset;

  xe::be<float>* cam_x =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(x_address);

  xe::be<float>* cam_y =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(y_address);

  float degree_x = RadianstoDegree(*cam_x);
  float degree_y = (float)*cam_y;

  // X-axis = 0 to 360
  if (cvars::invert_x) {
    degree_x += (input_state.mouse.x_delta / 7.5f) * (float)cvars::sensitivity;

  } else {
    degree_x -= (input_state.mouse.x_delta / 7.5f) * (float)cvars::sensitivity;
  }
  *cam_x = DegreetoRadians(degree_x);

  // Y-axis = -90 to 90
  if (cvars::invert_y) {
    degree_y -= (input_state.mouse.y_delta / 7.5f) * (float)cvars::sensitivity;
  } else {
    degree_y += (input_state.mouse.y_delta / 7.5f) * (float)cvars::sensitivity;
  }
  *cam_y = degree_y;

  return true;
}

std::string PerfectDarkZeroGame::ChooseBinds() { return "Default"; }

bool PerfectDarkZeroGame::ModifierKeyHandler(uint32_t user_index,
                                             RawInputState& input_state,
                                             X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe