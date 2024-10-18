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

#include "xenia/hid/winkey/hookables/hookable_game.h"

namespace xe {
namespace hid {
namespace winkey {

class PerfectDarkZeroGame : public HookableGame {
 public:
  enum class GameBuild {
    Unknown,
    PerfectDarkZero_TU0,
  };

  ~PerfectDarkZeroGame() override;

  bool IsGameSupported();

  float RadianstoDegree(float radians);
  float DegreetoRadians(float degree);

  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);

  bool IsPaused();

  bool InCover();

  std::string ChooseBinds();

  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);

 private:
  GameBuild game_build_ = GameBuild::Unknown;

  float centering_speed_ = 0.0125f;
  bool start_centering_ = false;
  bool disable_sway_ = false;  // temporarily prevents sway being applied
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_PerfectDarkZero_H_
