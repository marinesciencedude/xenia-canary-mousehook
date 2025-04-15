/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_PerfectDarkZero_H_
#define XENIA_HID_WINKEY_PerfectDarkZero_H_

#include "xenia/base/chrono.h"
#include "xenia/hid/winkey/hookables/hookable_game.h"

namespace xe {
namespace hid {
namespace winkey {

class PerfectDarkZeroGame : public HookableGame {
 public:
  enum class GameBuild {
    Unknown,
    PerfectDarkZero_TU0,
    PerfectDarkZero_TU3,
    PerfectDarkZero_PlatinumHitsTU15
  };

  ~PerfectDarkZeroGame() override;

  bool IsGameSupported(GameVersion title_version);

  float RadianstoDegree(float radians);
  float DegreetoRadians(float degree);

  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);

  bool IsPaused(xe::be<uint32_t>* player);

  bool isSpecialCam(xe::be<uint32_t>* player, uint32_t special_cam_flag_offset,
                    bool universal_addr = false, uint8_t cam_type = -1);

  void HandleRightStickEmulation(RawInputState& input_state,
                                 X_INPUT_STATE* out_state, bool LSmode = false);

  std::string ChooseBinds();

  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);
  void WeaponSwitchHandler(uint32_t user_index, RawInputState& input_state,
                           X_INPUT_STATE* out_state, int weapon,
                           uint16_t buttons);

  void MidHookInit();

 private:
  GameBuild game_build_ = GameBuild::Unknown;

  float centering_speed_ = 0.0125f;
  bool start_centering_ = false;
  bool disable_sway_ = false;  // temporarily prevents sway being applied
  std::chrono::steady_clock::time_point last_movement_time_x_;
  std::chrono::steady_clock::time_point last_movement_time_y_;
  static xe::be<uint32_t> fovscale_address;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_PerfectDarkZero_H_
