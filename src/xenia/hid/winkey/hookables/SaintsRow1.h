#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_SaintsRow1_H_
#define XENIA_HID_WINKEY_SaintsRow1_H_
#include <chrono>  // Include for chrono timing
#include "xenia/hid/winkey/hookables/hookable_game.h"

namespace xe {
namespace hid {
namespace winkey {

class SaintsRow1Game : public HookableGame {
 public:
  enum class GameBuild { Unknown, SaintsRow1_TU1, SaintsRow1_JP };

  ~SaintsRow1Game() override;

  bool IsGameSupported(GameVersion title_version);

  float RadianstoDegree(float radians);
  float DegreetoRadians(float degree);

  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);
  bool isTervelPlugin();
  bool inFirstPerson();
  bool isMP();
  bool isPaused();
  bool RotatePlayerinCustomization(RawInputState& input_state);
  bool CantSwitchWeapons();
  void WeaponWheelScrollWheel(RawInputState& input_state);
  bool inMapScreen();
  void MapCursor(RawInputState& input_state);
  void call_argless_function(uint32_t function_address);
  std::string ChooseBinds();
  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);
  bool isAnimStatus(uint8_t type);
  bool IsPlayerStatus1(uint32_t type);
  void SelectableWeaponsHack();
  void WeaponSwitchHandler(uint32_t user_index, RawInputState& input_state,
                           X_INPUT_STATE* out_state, int weapon,
                           uint16_t buttons);

 private:
  GameBuild game_build_ = GameBuild::Unknown;
  // Timer variables to hold the state for a while // this is probably not ideal
  // -Clippy95
  std::chrono::steady_clock::time_point last_movement_time_x_;
  std::chrono::steady_clock::time_point last_movement_time_y_;
  uint8_t tervelplugin_status;
  uint8_t* wheel_status;
  uint32_t player;
  uint8_t vehicle_status;
  uint8_t canspinplayer;
  enum animstatus {
    JUMPING = 2,
    PASSANGER = 10,
    DRIVING = 11,
    DEAD = 7,
    RAGDOLL = 4,

  };
  enum playerstatus1 {
    NORMAL = 4287,
    SPRINTING = 178,
    JUMPING1 = 8,
    DRIVING1 = 4304,
    STANDINGUP = 32,
    // this could be from interacting with menus or opening doors, or getting in
    // a car..
    BUSY = 0,

  };
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_SaintsRow1_H_
