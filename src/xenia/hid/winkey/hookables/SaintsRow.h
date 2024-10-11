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

#include "xenia/hid/winkey/hookables/hookable_game.h"

namespace xe {
namespace hid {
namespace winkey {

class SaintsRowGame : public HookableGame {
 public:
  enum class GameBuild { Unknown, SaintsRow2_TU3 };

  ~SaintsRowGame() override;

  bool IsGameSupported();

  float RadianstoDegree(float radians);
  float DegreetoRadians(float degree);

  bool DoHooks(uint32_t user_index, RawInputState& input_state,
               X_INPUT_STATE* out_state);

  bool ModifierKeyHandler(uint32_t user_index, RawInputState& input_state,
                          X_INPUT_STATE* out_state);

 private:
  GameBuild game_build_ = GameBuild::Unknown;
};

}  // namespace winkey
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_WINKEY_SaintsRow_H_
