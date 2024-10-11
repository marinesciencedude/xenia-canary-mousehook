/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_CallOfDuty_H_
#define XENIA_HID_WINKEY_CallOfDuty_H_

#include "xenia/hid/winkey/hookables/hookable_game.h"

namespace xe {
namespace hid {
namespace winkey {

class CallOfDutyGame : public HookableGame {
 public:
  enum class GameBuild {
    Unknown = 0,
    CallOfDuty4_SP,
    CallOfDuty4_TU0_MP,
    CallOfDuty4_TU4_MP,
    CallOfDuty4_Alpha_253SP,
    CallOfDuty4_Alpha_253SP_exe,
    CallOfDuty4_Alpha_253MP,
    CallOfDuty4_Alpha_253MP_exe,
    CallOfDuty4_Alpha_270SP,
    CallOfDuty4_Alpha_270SP_exe,
    CallOfDuty4_Alpha_270MP,
    CallOfDuty4_Alpha_270MP_exe,
    CallOfDuty4_Alpha_290SP,
    CallOfDuty4_Alpha_290SP_exe,
    CallOfDuty4_Alpha_290MP,
    CallOfDuty4_Alpha_290MP_exe,
    CallOfDuty4_Alpha_328SP,
    CallOfDuty4_Alpha_328SP_exe,
    CallOfDuty4_Alpha_328MP,
    CallOfDuty4_Alpha_328MP_exe,
    CallOfDutyMW2_Alpha_482SP,
    CallOfDutyMW2_Alpha_482MP,
    CallOfDutyMW2_TU0_SP,
    CallOfDuty3_SP,
    CallOfDuty3_MP_TU0,
    CallOfDuty3_MP_TU3,
    New_Moon_PatchedXEX,
    CallOfDutyMW3_TU0_MP,
    CallOfDutyMW2_TU0_MP,
    CallOfDutyNX1_Nightly_SP_maps,
    CallOfDutyNX1_nx1sp,
    CallOfDutyNX1_nx1mp_demo,
    CallOfDutyNX1_nx1mp,
    CallOfDutyNX1_NightlyMPmaps,
    CallOfDutyWaW_TU7_SP,
    CallOfDutyWaW_TU7_MP,
    CallOfDutyGhosts_IW6_DEV_2iw6mp,
    CallOfDutyGhosts_IW6_DEV_1iw6sp,
    CallOfDutyGhosts_IW6_DEV_4iw6sp_demo,
    CallOfDutyGhosts_IW6_DEV_default,
    CallOfDutyAdvancedWarfare_TU17_SP,
    CallOfDutyAdvancedWarfare_TU17_MP,
    Wolfenstein_TU0_SP,
  };

  ~CallOfDutyGame() override;

  bool IsGameSupported();

  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);

  std::string ChooseBinds();

  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);

  bool Dvar_GetBool(std::string dvar, uint32_t dvar_address);

 private:
  GameBuild game_build_ = GameBuild::Unknown;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_CallOfDuty_H_
