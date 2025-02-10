/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_GTAV_H_
#define XENIA_HID_WINKEY_GTAV_H_
#define DTOR 0.01745329251f
#include "xenia/base/chrono.h"
#include "xenia/hid/winkey/hookables/hookable_game.h"
namespace xe {
namespace hid {
namespace winkey {

class GTAVGame : public HookableGame {
 public:
  // enum class GameBuild { Unknown, GTAV_TU26 };

  ~GTAVGame() override;

  bool IsGameSupported();

  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);

  void GetAddressesFromHelperPlugin(uint32_t* pullaround, uint32_t* delta_x,
                                    uint32_t* delta_y,
                                    uint32_t* wheel_delta = NULL,
                                    uint32_t* player_status = NULL);

  void HandleMouseInput(uint8_t* mousehook_ShouldPullAroundWhenUsingMouse,
                        float delta_x, float delta_y);

  std::string ChooseBinds();

  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);

  void WeaponSwitchHandler(uint32_t user_index, RawInputState& input_state,
                           X_INPUT_STATE* out_state, int weapon,
                           uint16_t buttons);

 private:
  // GameBuild game_build_ = GameBuild::Unknown;
  std::chrono::steady_clock::time_point last_mouse_input_time_;
  bool is_mouse_input_active_ = false;
  enum VehicleType {
    VEHICLE_TYPE_NONE =
        420,  // Actually 0xFFFFFFFF ingame but plugin does 420 ok
    VEHICLE_TYPE_NONE_AND_PAUSED = 421,  // Plugin thing for modifier
    VEHICLE_TYPE_CAR = 0x0,
    VEHICLE_TYPE_PLANE = 0x1,
    VEHICLE_TYPE_HELI = 0x8,
    VEHICLE_TYPE_BIKE = 0x9,
    VEHICLE_TYPE_BMX = 0xA,
  };
  bool
      GTAVMousehookHelperXenia_pluginloaded;  // My helper plugin to make camera
                                              // hooks to disable auto-center
                                              // appropriability - Clippy95.
  uint32_t mousehook_ShouldPullAroundWhenUsingMouse_addr = NULL;
  uint32_t plugin_delta_x_addr = NULL;
  uint32_t plugin_delta_y_addr = NULL;
  uint32_t plugin_delta_wheel_delta_addr = NULL;
  uint32_t player_status_addr = NULL;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_GTAV_H_
