/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/Crackdown2.h"

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

const uint32_t kTitleIdCrackdown2 = 0x4D5308BC;

namespace xe {
namespace hid {
namespace winkey {
struct GameBuildAddrs {
  uint32_t base_address;
  std::string title_version;
  uint32_t x_offset;
  uint32_t y_offset;
};

std::map<Crackdown2Game::GameBuild, GameBuildAddrs> supported_builds{
    {Crackdown2Game::GameBuild::Crackdown2_TU0,
     {0x836C6520, "1.0", 0x7EC, 0x7E8}},
    {Crackdown2Game::GameBuild::Crackdown2_TU5,
     {0x83800F88, "1.0.5", 0x7EC, 0x7E8}}};

Crackdown2Game::~Crackdown2Game() = default;

bool Crackdown2Game::IsGameSupported() {
  if (kernel_state()->title_id() != kTitleIdCrackdown2) {
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

float Crackdown2Game::DegreetoRadians(float degree) {
  return (float)(degree * (M_PI / 180));
}

float Crackdown2Game::RadianstoDegree(float radians) {
  return (float)(radians * (180 / M_PI));
}

bool Crackdown2Game::DoHooks(uint32_t user_index, RawInputState& input_state,
                             X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  if (supported_builds.count(game_build_) == 0) {
    return false;
  }

  // Don't constantly write if there is no mouse movement.
  if (input_state.mouse.x_delta == 0 || input_state.mouse.y_delta == 0) {
    return false;
  }

  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
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

  xe::be<float>* radian_x =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(x_address);

  xe::be<float>* radian_y =
      kernel_memory()->TranslateVirtual<xe::be<float>*>(y_address);

  float degree_x = RadianstoDegree(*radian_x);
  float degree_y = RadianstoDegree(*radian_y);

  // X-axis = 0 to 360
  if (!cvars::invert_x) {
    degree_x += (input_state.mouse.x_delta / 50.f) * (float)cvars::sensitivity;
    *radian_x = DegreetoRadians(degree_x);
  } else {
    degree_x -= (input_state.mouse.x_delta / 50.f) * (float)cvars::sensitivity;
    *radian_x = DegreetoRadians(degree_x);
  }

  // Y-axis = -90 to 90
  if (!cvars::invert_y) {
    degree_y += (input_state.mouse.y_delta / 50.f) * (float)cvars::sensitivity;
  } else {
    degree_y -= (input_state.mouse.y_delta / 50.f) * (float)cvars::sensitivity;
  }

  *radian_y = DegreetoRadians(degree_y);

  return true;
}

std::string Crackdown2Game::ChooseBinds() { return "Default"; }

bool Crackdown2Game::ModifierKeyHandler(uint32_t user_index,
                                        RawInputState& input_state,
                                        X_INPUT_STATE* out_state) {
  return false;
}
}  // namespace winkey
}  // namespace hid
}  // namespace xe