/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/hookables/SourceEngine.h"

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
DECLARE_double(source_sniper_sensitivity);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);
DECLARE_int32(walk_orthogonal);
DECLARE_int32(walk_diagonal);

const uint32_t kTitleIdCSGO = 0x5841125A;
const uint32_t kTitleIdL4D1 = 0x45410830;
const uint32_t kTitleIdL4D2 = 0x454108D4;
const uint32_t kTitleIdOrangeBox = 0x4541080F;
const uint32_t kTitleIdPortalSA = 0x58410960;
const uint32_t kTitleIdPortal2 = 0x45410912;
const uint32_t kTitleIdBloodyGoodTime = 0x584109B3;
const uint32_t kTitleIdDarkMessiah = 0x55530804;

namespace xe {
namespace hid {
namespace winkey {

bool __inline IsKeyToggled(uint8_t key) {
  return (GetKeyState(key) & 0x1) == 0x1;
}

SourceEngine::SourceEngine() {
  original_sensitivity = cvars::sensitivity;
  engine_360 = NULL;
};

SourceEngine::~SourceEngine() = default;

struct GameBuildAddrs {
  uint32_t title_id;
  std::string title_version;
  uint32_t execute_addr;
  uint32_t angle_offset;
};

// Replace with build names when we introduce more compatibility
std::map<SourceEngine::GameBuild, GameBuildAddrs> supported_builds{
    {SourceEngine::GameBuild::CSGO, {kTitleIdCSGO, "5.0", 0x86955490, 0x4AE8}},
    {SourceEngine::GameBuild::CSGO_Beta,
     {kTitleIdCSGO, "1.0.1.16", 0x8697DB30, 0x4AC8}},
    {SourceEngine::GameBuild::L4D1, {kTitleIdL4D1, "1.0", 0x86536888, 0x4B44}},
    {SourceEngine::GameBuild::L4D1_GOTY,
     {kTitleIdL4D1, "6.0", 0x86537FA0, 0x4B44}},
    {SourceEngine::GameBuild::L4D2, {kTitleIdL4D2, "3.0", 0x86CC4E60, 0x4A94}},
    {SourceEngine::GameBuild::OrangeBox,
     {kTitleIdOrangeBox, "4.0", NULL, 0x863F53A8}},
    {SourceEngine::GameBuild::PortalSA,
     {kTitleIdPortalSA, "3.0.1", NULL, 0x863F56B0}},
    {SourceEngine::GameBuild::Portal2,
     {kTitleIdPortal2, "4.0", 0x82C50180, 0x4A98}},
    {SourceEngine::GameBuild::Portal2_TU1,
     {kTitleIdPortal2, "4.0.1", 0x82C50220, 0x4A98}},
    {SourceEngine::GameBuild::Postal3,
     {kTitleIdOrangeBox, "1.0.1.16", NULL, 0x86438700}},
    {SourceEngine::GameBuild::BloodyGoodTime,
     {kTitleIdBloodyGoodTime, "3.0", NULL, 0x8644A6B0}},
    {
        SourceEngine::GameBuild::DarkMessiah,
        {kTitleIdDarkMessiah, "5.0", 0x856FC050, 0x856E2490}
        // default.xex, DMMulti_m.xex
    },
};

bool SourceEngine::IsGameSupported() {
  auto title_id = kernel_state()->title_id();
  if (title_id != kTitleIdCSGO && title_id != kTitleIdL4D1 &&
      title_id != kTitleIdL4D2 && title_id != kTitleIdOrangeBox &&
      title_id != kTitleIdPortalSA && title_id != kTitleIdPortal2 &&
      title_id != kTitleIdBloodyGoodTime && title_id != kTitleIdDarkMessiah)
    return false;

  const std::string current_version =
      kernel_state()->emulator()->title_version();

  for (auto& build : supported_builds) {
    if (title_id == build.second.title_id &&
        current_version == build.second.title_version) {
      game_build_ = build.first;
      return true;
    }
  }

  return false;
}

#define IS_KEY_DOWN(x) (input_state.key_states[x])

bool SourceEngine::DoHooks(uint32_t user_index, RawInputState& input_state,
                           X_INPUT_STATE* out_state) {
  if (!IsGameSupported()) {
    return false;
  }

  // Wait until module is loaded, if loaded don't check again otherwise it will
  // impact performance.
  if (!engine_360) {
    if (!kernel_state()->GetModule("engine_360.dll")) {
      return false;
    } else {
      engine_360 = true;
    }
  }

  XThread* current_thread = XThread::GetCurrentThread();

  if (!current_thread) {
    return false;
  }

  // Swap execute_addr if singleplayer, and set execute_addr to null
  if (game_build_ == GameBuild::DarkMessiah) {
    if (kernel_state()->GetModule("default.xex") &&
        supported_builds[game_build_].execute_addr) {
      supported_builds[game_build_].angle_offset =
          supported_builds[game_build_].execute_addr;
    }

    supported_builds[game_build_].execute_addr = 0;
  }

  uint32_t player_ptr;
  if (supported_builds[game_build_].execute_addr) {
    current_thread->thread_state()->context()->r[3] = -1;

    kernel_state()->processor()->Execute(
        current_thread->thread_state(),
        supported_builds[game_build_].execute_addr);

    // Get player pointer
    player_ptr =
        static_cast<uint32_t>(current_thread->thread_state()->context()->r[3]);

    if (!player_ptr) {
      // Not in game
      return false;
    }
  }

  xe::be<uint32_t>* angle_offset;

  if (supported_builds[game_build_].execute_addr) {
    angle_offset = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
        player_ptr + supported_builds[game_build_].angle_offset);
  } else {
    angle_offset = kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
        supported_builds[game_build_].angle_offset);
  }

  QAngle* ang = reinterpret_cast<QAngle*>(angle_offset);

  // std::string angState = fmt::format("Angle: y: {} x: {} z: {}", ang->pitchY,
  //                                    ang->pitchX, ang->yaw);

  // Have to do weird things converting it to normal float otherwise
  // xe::be += treats things as int?
  float camX = (float)ang->pitchX;
  float camY = (float)ang->pitchY;

  if (cvars::source_sniper_sensitivity != 0) {
    if (IsKeyToggled(VK_CAPITAL) != 0) {
      cvars::sensitivity = cvars::source_sniper_sensitivity;
    } else {
      cvars::sensitivity = original_sensitivity;
    }
  }

  if (!cvars::invert_x) {
    camX -=
        (((float)input_state.mouse.x_delta) / 7.5f) * (float)cvars::sensitivity;
  } else {
    camX +=
        (((float)input_state.mouse.x_delta) / 7.5f) * (float)cvars::sensitivity;
  }

  if (!cvars::invert_y) {
    camY +=
        (((float)input_state.mouse.y_delta) / 7.5f) * (float)cvars::sensitivity;
  } else {
    camY -=
        (((float)input_state.mouse.y_delta) / 7.5f) * (float)cvars::sensitivity;
  }

  ang->pitchX = camX;
  ang->pitchY = camY;

  return true;
}

// probably making a mistake using a template, ah well
template <typename T>
int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

bool SourceEngine::ModifierKeyHandler(uint32_t user_index,
                                      RawInputState& input_state,
                                      X_INPUT_STATE* out_state) {
  float thumb_lx = (int16_t)out_state->gamepad.thumb_lx;
  float thumb_ly = (int16_t)out_state->gamepad.thumb_ly;

  // Work out angle from the current stick values
  float angle = atan2f(thumb_ly, thumb_lx);

  // Equates to 134.99 h.u./s - 22800 for forward/backward, 18421 for diagonal
  int16_t distance_x, distance_y;

  // as soon as you put it to a separate variable it stops bugging out

  int multiplier_x = sgn(thumb_lx);
  int multiplier_y = sgn(thumb_ly);

  // If angle DIV π⁄4 is odd
  if (fmod(angle / 0.785398185f, 2) != 0) {
    distance_x = int16_t(cvars::walk_diagonal * multiplier_x);
    distance_y = int16_t(cvars::walk_diagonal * multiplier_y);
  } else {
    // Default value equates to 134.99 h.u./s, any higher than this value and
    // the movement speed immediately goes to max
    distance_x = int16_t(cvars::walk_orthogonal *
                         multiplier_x);  // Default value between SHRT_MAX *
                                         // (177.4/255 and 177.5/255)
    distance_y = int16_t(cvars::walk_orthogonal *
                         multiplier_y);  // Default value between SHRT_MAX *
                                         // (177.4/255 and 177.5/255)
  }

  // Need analogue-compatible version
  // out_state->gamepad.thumb_lx = (int16_t)(distance * cosf(angle));
  // out_state->gamepad.thumb_ly = (int16_t)(distance * sinf(angle));
  out_state->gamepad.thumb_lx = distance_x;
  out_state->gamepad.thumb_ly = distance_y;

  // Return true to signal that we've handled the modifier, so default modifier
  // won't be used
  return true;
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
