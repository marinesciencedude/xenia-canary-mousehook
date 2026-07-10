/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GAME_LAUNCH_HOOKS_H_
#define XENIA_GAME_LAUNCH_HOOKS_H_

#include "xenia/base/delegate.h"

namespace xe {
namespace kernel {
class UserModule;
}  // namespace kernel

class GameLaunchHooks {
 public:
  static xe::Delegate<kernel::UserModule*>& OnPreLaunch() {
    static xe::Delegate<kernel::UserModule*> on_pre_launch;
    return on_pre_launch;
  }
};

}  // namespace xe

#endif  // XENIA_GAME_LAUNCH_HOOKS_H_
