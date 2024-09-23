/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_RedDeadRedemption_H_
#define XENIA_HID_WINKEY_RedDeadRedemption_H_

#include "xenia/base/chrono.h"
#include "xenia/hid/winkey/hookables/hookable_game.h"

namespace xe {
namespace hid {
namespace winkey {

class RedDeadRedemptionGame : public HookableGame {
 public:
  enum /* class*/ GameBuild {
    Unknown,
    RedDeadRedemption_GOTY_Disk1,
    RedDeadRedemption_GOTY_Disk2,
    RedDeadRedemption_Original_TU0,
    RedDeadRedemption_Original_TU9,
    RedDeadRedemption_UndeadNightmare_Standalone_TU4
  };

  ~RedDeadRedemptionGame() override;

  bool IsGameSupported();

  float RadianstoDegree(float radians);
  float DegreetoRadians(float degree);
  bool IsWeaponWheelShown();
  void HandleWeaponWheelEmulation(RawInputState& input_state,
                                  X_INPUT_STATE* out_state);
  bool IsCinematicTypeEnabled();
  bool IsPaused();
  void HandleRightStickEmulation(RawInputState& input_state,
                                 X_INPUT_STATE* out_state);
  float ClampVerticalAngle(float degree_y);
  uint8_t GetCamType();

  uint32_t FindPatternWithWildcardAddress(uint32_t start_address,
                                          uint32_t end_address,
                                          const std::vector<uint8_t>& pattern);

  bool CompareMemoryWithPattern(const uint8_t* memory,
                                const std::vector<uint8_t>& pattern);

  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);

  std::string ChooseBinds();

  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);

 private:
  GameBuild game_build_ = GameBuild::Unknown;
  std::chrono::steady_clock::time_point last_movement_time_x_;
  std::chrono::steady_clock::time_point last_movement_time_y_;
  static uint32_t cached_carriage_x_address;
  static uint32_t cached_carriage_y_address;
  static uint32_t cached_carriage_z_address;
  static uint32_t cached_auto_center_strength_address_carriage;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_RedDeadRedemption_H_
