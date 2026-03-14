/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_FRONTEND_H_
#define XENIA_CPU_PPC_PPC_FRONTEND_H_

#include <memory>
#include <unordered_map>

#include "xenia/base/type_pool.h"
#include "xenia/cpu/function.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace ppc {

class PPCTranslator;

struct PPCBuiltins {
  int32_t global_lock_count;
  Function* check_global_lock;
  Function* enter_global_lock;
  Function* leave_global_lock;
  Function* syscall_handler;
};

class PPCFrontend {
 public:
  explicit PPCFrontend(Processor* processor);
  ~PPCFrontend();

  bool Initialize();

  Processor* processor() const { return processor_; }
  Memory* memory() const;
  PPCBuiltins* builtins() { return &builtins_; }

  bool DeclareFunction(GuestFunction* function);
  bool DefineFunction(GuestFunction* function, uint32_t debug_info_flags);

  // Returns (creating if needed) a per-address builtin for the mid-hook at
  // the given guest address. The address is baked into arg0 so the handler
  // never needs to touch scratch.
  Function* GetOrCreateMidHookBuiltin(uint32_t address);

 private:
  Processor* processor_;
  PPCBuiltins builtins_ = {0};
  TypePool<PPCTranslator, PPCFrontend*> translator_pool_;
  std::unordered_map<uint32_t, Function*> midhook_builtins_;
};
// Checks the state of the global lock and sets scratch to the current MSR
// value.
void CheckGlobalLock(PPCContext* ppc_context, void* arg0, void* arg1);

using MouseHookMidHook = void (*)(PPCContext* context, void* arg0, void* arg1);
void RegisterMidHookASM(uint32_t address, MouseHookMidHook hook_function);
bool HasMidHookAt(uint32_t address);
}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_FRONTEND_H_
