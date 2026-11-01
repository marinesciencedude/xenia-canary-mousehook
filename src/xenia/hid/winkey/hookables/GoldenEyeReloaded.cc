/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define _USE_MATH_DEFINES

#include "xenia/hid/winkey/hookables/GoldenEyeReloaded.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "xenia/base/platform_win.h"
#include "xenia/emulator.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/xbox.h"

using namespace xe::kernel;

DECLARE_double(sensitivity);
DECLARE_double(fov_sensitivity);
DECLARE_bool(invert_y);
DECLARE_bool(invert_x);

DEFINE_bool(ge_reloaded_aim_assist, false,
            "(GoldenEye 007 Reloaded) Keep the game's controller aim assist "
            "(aim snap, lock-on and target tracking) while using the mouse",
            "MouseHook");

DEFINE_double(ge_reloaded_vehicle_sensitivity, 1.0,
              "(GoldenEye 007 Reloaded) Mouse sensitivity in vehicles (e.g. "
              "the Dam truck), where the mouse moves the right stick",
              "MouseHook");

const uint32_t kTitleIdGoldenEyeReloaded = 0x415608AF;

namespace xe {
namespace hid {
namespace winkey {

// Mouse look is injected into the player look update after the game's stick
// processing, so there's no dead zone, response curve or turn speed cap (hook
// points from github.com/nikolaygorb/GoldenEye007ReloadedRecomp). Mouse motion
// is taken straight from raw input at each look update, so every update turns
// the camera by exactly the motion since the previous one, 1:1 at any
// framerate.
namespace {

struct ReloadedBuildAddrs {
  // Instructions that must match, one per hook point.
  std::vector<std::pair<uint32_t, uint32_t>> checks;

  // lbz r11,2618(r31): aim lock / snap checks, the look update returns right
  // after while either is set.
  uint32_t aim_assist_hook_addr;
  // Right after the aim friction call: r1+104 holds the yaw rate (rad/s).
  uint32_t yaw_hook_addr;
  // Right after the aim tracking call: r1+96 holds its yaw correction.
  uint32_t tracking_hook_addr;
  // The pitch clamp every look update path ends in (r31+2140 is loaded).
  uint32_t pitch_hook_addr;
};

const std::map<GoldenEyeReloadedGame::GameBuild, ReloadedBuildAddrs>
    kSupportedBuilds = {
        {GoldenEyeReloadedGame::GameBuild::GoldenEyeReloaded_Retail,
         {{{0x821FDD00, 0x897F0A3A},
           {0x821FDD64, 0x39610068},
           {0x821FDD84, 0x39410080},
           {0x821FDEE4, 0xC01F085C}},
          0x821FDD00,
          0x821FDD64,
          0x821FDD84,
          0x821FDEE4}},
};

// Offsets into the player struct (r31 in the look update).
constexpr uint32_t kPlayerDeltaTime = 428;       // float, seconds
constexpr uint32_t kPlayerPitch = 2140;          // float, radians, + = down
constexpr uint32_t kPlayerAiming = 2242;         // uint8, aim button held
constexpr uint32_t kPlayerFov = 2576;            // float, radians
constexpr uint32_t kPlayerAimLock = 2618;        // uint8
constexpr uint32_t kPlayerAimLockCached = 2661;  // uint8
constexpr uint32_t kPlayerAimSnap = 2662;        // uint8
// Offsets into the look update's stack frame (r1).
constexpr uint32_t kStackTrackingYaw = 96;  // float
constexpr uint32_t kStackYawRate = 104;     // float, rad/s, + = right

// Same scale as the Call of Duty hooks: degrees per mouse count at
// sensitivity 1.
constexpr double kCountsPerDegree = 10.5;
// Mouse motion (|dx| + |dy|) that cancels an aim snap / lock when aim assist
// is kept.
constexpr double kSnapCancelCounts = 4.0;
// Guards against a stalled frame turning into one huge snap.
constexpr double kMaxRadiansPerUpdate = 1.5;
// If the look update hasn't run for this long we're in a menu / cutscene /
// vehicle; motion from then is dropped instead of turning the camera later.
constexpr std::chrono::milliseconds kLookUpdateTimeout{300};
// If the on-foot look update hasn't run for this long, the camera is driven
// by something else (vehicles such as the Dam truck), which reads the right
// stick, so the mouse moves the right stick instead.
constexpr std::chrono::milliseconds kStickFallbackDelay{100};
// Right stick emulation: deflection just past the game's dead zone, plus
// deflection per mouse count in one controller poll.
constexpr double kStickDeadZone = 0.27;
constexpr double kStickPerCount = 0.03;

std::mutex mouse_mutex_;
double pending_dx_ = 0.0;
double pending_dy_ = 0.0;
std::chrono::steady_clock::time_point last_look_update_{};

// FOV while not aiming, zoom sensitivity is relative to it.
double hip_fov_ = 0.0;

// Pitch taken by the yaw hook, applied by the pitch hook of the same update.
thread_local double pending_pitch_ = 0.0;
thread_local bool pitch_pending_ = false;

const ReloadedBuildAddrs* active_build_ = nullptr;

float LoadFloat(uint32_t address) {
  return *kernel_memory()->TranslateVirtual<xe::be<float>*>(address);
}

void StoreFloat(uint32_t address, float value) {
  *kernel_memory()->TranslateVirtual<xe::be<float>*>(address) = value;
}

uint8_t* BytePtr(uint32_t address) {
  return kernel_memory()->TranslateVirtual<uint8_t*>(address);
}

bool HookActive() {
  return active_build_ &&
         kernel_state()->title_id() == kTitleIdGoldenEyeReloaded;
}

// Moves raw mouse motion into the pending turn, called at the start of each
// look update.
void CollectMouseMotion() {
  int64_t dx, dy;
  TakeRawMouseMotion(dx, dy);

  std::lock_guard<std::mutex> lock(mouse_mutex_);
  const auto now = std::chrono::steady_clock::now();
  if (now - last_look_update_ > kLookUpdateTimeout) {
    pending_dx_ = pending_dy_ = 0.0;
  } else {
    pending_dx_ += static_cast<double>(dx);
    pending_dy_ += static_cast<double>(dy);
  }
  last_look_update_ = now;
}

float WrapAngle(double angle) {
  return static_cast<float>(std::remainder(angle, 2.0 * M_PI));
}

bool PlausibleFov(double fov) {
  return std::isfinite(fov) && fov > 0.02 && fov < 3.0;
}

// 1 when not aiming; while aiming through a zoomed scope, scales by how much
// the view is zoomed in, like cg_fovscale in the COD hooks.
double ZoomScale(uint32_t player) {
  const double fov = LoadFloat(player + kPlayerFov);
  if (!PlausibleFov(fov)) {
    return 1.0;
  }
  if (!*BytePtr(player + kPlayerAiming)) {
    hip_fov_ = fov;
    return 1.0;
  }
  if (!PlausibleFov(hip_fov_)) {
    return 1.0;
  }
  double scale = std::tan(fov * 0.5) / std::tan(hip_fov_ * 0.5);
  if (scale >= 0.999) {
    return 1.0;
  }
  scale = std::clamp(scale, 0.02, 1.0);
  const double a = cvars::fov_sensitivity;
  return (1.0 - a) * (scale * scale) + a * scale;
}

void AimAssistHook(PPCContext* context, void* arg0, void* arg1) {
  if (!HookActive()) {
    return;
  }
  CollectMouseMotion();

  const uint32_t player = static_cast<uint32_t>(context->r[31]);
  uint8_t* lock = BytePtr(player + kPlayerAimLock);
  uint8_t* snap = BytePtr(player + kPlayerAimSnap);
  if (!*lock && !*snap) {
    return;
  }

  bool cancel = !cvars::ge_reloaded_aim_assist;
  if (!cancel) {
    // Moving the mouse ends the aim snap / lock, as moving the stick does.
    std::lock_guard<std::mutex> guard(mouse_mutex_);
    cancel = std::abs(pending_dx_) + std::abs(pending_dy_) >= kSnapCancelCounts;
  }
  if (cancel) {
    *lock = 0;
    *BytePtr(player + kPlayerAimLockCached) = 0;
    *snap = 0;
  }
}

void YawHook(PPCContext* context, void* arg0, void* arg1) {
  pitch_pending_ = false;
  if (!HookActive()) {
    return;
  }
  CollectMouseMotion();

  double dx, dy;
  {
    std::lock_guard<std::mutex> lock(mouse_mutex_);
    dx = pending_dx_;
    dy = pending_dy_;
    pending_dx_ = pending_dy_ = 0.0;
  }

  const uint32_t player = static_cast<uint32_t>(context->r[31]);
  const double zoom = ZoomScale(player);
  const double dt = LoadFloat(player + kPlayerDeltaTime);
  if (!(dt > 1e-4 && dt < 0.5) || (dx == 0.0 && dy == 0.0)) {
    return;  // paused / stalled frame, or nothing to do
  }

  const double radians_per_count =
      (cvars::sensitivity / kCountsPerDegree) * (M_PI / 180.0) * zoom;
  double yaw = dx * radians_per_count * (cvars::invert_x ? -1.0 : 1.0);
  double pitch = dy * radians_per_count * (cvars::invert_y ? -1.0 : 1.0);
  if (!std::isfinite(yaw) || !std::isfinite(pitch)) {
    return;
  }
  yaw = std::clamp(yaw, -kMaxRadiansPerUpdate, kMaxRadiansPerUpdate);
  pitch = std::clamp(pitch, -kMaxRadiansPerUpdate, kMaxRadiansPerUpdate);

  // The game turns by the yaw rate times the frame time.
  const uint32_t rate_address =
      static_cast<uint32_t>(context->r[1]) + kStackYawRate;
  StoreFloat(rate_address,
             LoadFloat(rate_address) + static_cast<float>(yaw / dt));
  pending_pitch_ = pitch;
  pitch_pending_ = true;
}

void TrackingHook(PPCContext* context, void* arg0, void* arg1) {
  if (!HookActive() || cvars::ge_reloaded_aim_assist) {
    return;
  }
  // Drop the aim assist's turn towards the tracked target.
  StoreFloat(static_cast<uint32_t>(context->r[1]) + kStackTrackingYaw, 0.0f);
}

void PitchHook(PPCContext* context, void* arg0, void* arg1) {
  if (!pitch_pending_) {
    return;
  }
  pitch_pending_ = false;
  if (!HookActive()) {
    return;
  }
  const uint32_t player = static_cast<uint32_t>(context->r[31]);
  const uint32_t address = player + kPlayerPitch;
  StoreFloat(address, LoadFloat(address) + static_cast<float>(pending_pitch_));
}

}  // namespace

GoldenEyeReloadedGame::~GoldenEyeReloadedGame() = default;

bool GoldenEyeReloadedGame::IsGameSupported(GameVersion title_version) {
  if (kernel_state()->title_id() != kTitleIdGoldenEyeReloaded) {
    return false;
  }

  if (game_build_ != GameBuild::Unknown) {
    return true;
  }

  for (auto& build : kSupportedBuilds) {
    const bool matches = std::all_of(
        build.second.checks.begin(), build.second.checks.end(),
        [](const std::pair<uint32_t, uint32_t>& check) {
          return *kernel_memory()->TranslateVirtual<xe::be<uint32_t>*>(
                     check.first) == check.second;
        });
    if (matches) {
      game_build_ = build.first;
      return true;
    }
  }

#ifdef XENIA_MOUSEHOOK_MESSAGE
  mousehook_message_wrapper(
      "MOUSEHOOK: Supported Title ID, but unsupported version of GoldenEye "
      "007: Reloaded, refer to the Github repo for supported titles.",
      true);
#endif
  return false;
}

bool GoldenEyeReloadedGame::DoHooks(uint32_t user_index,
                                    RawInputState& input_state,
                                    X_INPUT_STATE* out_state) {
  // On foot, mouse motion is taken from raw input by the look update hooks.
  {
    std::lock_guard<std::mutex> lock(mouse_mutex_);
    if (std::chrono::steady_clock::now() - last_look_update_ <
        kStickFallbackDelay) {
      return false;
    }
  }

  const int32_t dx = input_state.mouse.x_delta;
  const int32_t dy = input_state.mouse.y_delta;
  if (!dx && !dy) {
    return false;
  }

  auto to_stick = [](int32_t counts) -> int16_t {
    if (!counts) {
      return 0;
    }
    const double magnitude = std::min(
        1.0, kStickDeadZone + std::abs(counts) * kStickPerCount *
                                  cvars::ge_reloaded_vehicle_sensitivity);
    return static_cast<int16_t>(std::copysign(magnitude, counts) * SHRT_MAX);
  };
  // Mouse right = stick right, mouse down = stick down (look down).
  if (dx) {
    out_state->gamepad.thumb_rx = to_stick(cvars::invert_x ? -dx : dx);
  }
  if (dy) {
    out_state->gamepad.thumb_ry = to_stick(cvars::invert_y ? dy : -dy);
  }
  return true;
}

std::string GoldenEyeReloadedGame::ChooseBinds() { return "Default"; }

bool GoldenEyeReloadedGame::ModifierKeyHandler(uint32_t user_index,
                                               RawInputState& input_state,
                                               X_INPUT_STATE* out_state) {
  return false;
}

void GoldenEyeReloadedGame::WeaponSwitchHandler(uint32_t user_index,
                                                RawInputState& input_state,
                                                X_INPUT_STATE* out_state,
                                                int weapon, uint16_t buttons) {}

void GoldenEyeReloadedGame::MidHookInit() {
  if (midhook_status == HOOKED || game_build_ == GameBuild::Unknown) {
    return;
  }

  active_build_ = &kSupportedBuilds.at(game_build_);
  xe::cpu::ppc::RegisterMidHookASM(active_build_->aim_assist_hook_addr,
                                   AimAssistHook);
  xe::cpu::ppc::RegisterMidHookASM(active_build_->yaw_hook_addr, YawHook);
  xe::cpu::ppc::RegisterMidHookASM(active_build_->tracking_hook_addr,
                                   TrackingHook);
  xe::cpu::ppc::RegisterMidHookASM(active_build_->pitch_hook_addr, PitchHook);

  midhook_status = HOOKED;
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
