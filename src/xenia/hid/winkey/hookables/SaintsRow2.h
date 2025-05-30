/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_SaintsRow_H_
#define XENIA_HID_WINKEY_SaintsRow_H_

#include <chrono>  // Include for chrono timing
#include "xenia/hid/winkey/hookables/hookable_game.h"

namespace xe {
namespace hid {
namespace winkey {

class SaintsRow2Game : public HookableGame {
 public:
  enum class GameBuild { Unknown, SaintsRow2_TU3 };

  ~SaintsRow2Game() override;

  bool IsGameSupported(GameVersion title_version);

  float RadianstoDegree(float radians);
  float DegreetoRadians(float degree);

  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);

  std::string ChooseBinds();

  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);

  void WeaponSwitchHandler(uint32_t user_index, RawInputState& input_state,
                           X_INPUT_STATE* out_state, int weapon,
                           uint16_t buttons);

  uint64_t reset_fineaim(uint32_t function_address, uint32_t player_ptr,
                         uint32_t a2, uint32_t a3);

 private:
  GameBuild game_build_ = GameBuild::Unknown;

  // Timer variables to hold the state for a while // this is probably not ideal
  // -Clippy95
  std::chrono::steady_clock::time_point last_movement_time_x_;
  std::chrono::steady_clock::time_point last_movement_time_y_;
  uint32_t player_status;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_SaintsRow_H_
