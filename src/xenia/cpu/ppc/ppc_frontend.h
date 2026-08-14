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

#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/type_pool.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/ppc/ppc_context.h"
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
  Function* GetOrCreateFunctionHookBuiltin(uint32_t address);

 private:
  Processor* processor_;
  PPCBuiltins builtins_ = {0};
  TypePool<PPCTranslator, PPCFrontend*> translator_pool_;
  std::unordered_map<uint32_t, Function*> midhook_builtins_;
  std::unordered_map<uint32_t, Function*> function_hook_builtins_;
};
// Checks the state of the global lock and sets scratch to the current MSR
// value.
void CheckGlobalLock(PPCContext* ppc_context, void* arg0, void* arg1);

using MouseHookMidHook = void (*)(PPCContext* context, void* arg0, void* arg1);
void RegisterMidHookASM(uint32_t address, MouseHookMidHook hook_function);
bool HasMidHookAt(uint32_t address);

namespace function_hook {

class FunctionHookBase {
 public:
  explicit FunctionHookBase(uint32_t address) : address_(address) {}
  virtual ~FunctionHookBase() = default;

  uint32_t address() const { return address_; }
  virtual void Invoke(PPCContext* context) = 0;

 private:
  uint32_t address_;
};

void RegisterFunctionHookImpl(uint32_t address,
                              std::unique_ptr<FunctionHookBase> hook);
bool HasFunctionHookAt(uint32_t address);
bool IsFunctionHookBypassed(uint32_t address);
void BeginFunctionHookBypass(uint32_t address);
void EndFunctionHookBypass(uint32_t address);
void ExecuteFunctionHookOriginal(PPCContext* context, uint32_t address);

struct HookArgCursor {
  size_t ordinal = 0;
  size_t fpr = 0;
};

template <typename T>
struct HookType {
  using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T>
using HookTypeT = typename HookType<T>::type;

template <typename T>
constexpr bool IsFloatingHookType =
    std::is_same_v<HookTypeT<T>, float> || std::is_same_v<HookTypeT<T>, double>;

template <typename T>
constexpr bool IsIntegerHookType =
    std::is_integral_v<HookTypeT<T>> || std::is_enum_v<HookTypeT<T>>;

template <typename T>
HookTypeT<T> ReadHookArg(PPCContext* context, HookArgCursor& cursor) {
  using ValueType = HookTypeT<T>;
  if constexpr (IsFloatingHookType<ValueType>) {
    assert_true(cursor.ordinal < 8);
    assert_true(cursor.fpr < 13);
    ++cursor.ordinal;
    return static_cast<ValueType>(context->f[1 + cursor.fpr++]);
  } else {
    static_assert(IsIntegerHookType<ValueType>,
                  "Function hooks currently support integer, enum, float and "
                  "double scalar arguments.");
    assert_true(cursor.ordinal < 8);
    if constexpr (std::is_enum_v<ValueType>) {
      using Underlying = std::underlying_type_t<ValueType>;
      return static_cast<ValueType>(
          static_cast<Underlying>(context->r[3 + cursor.ordinal++]));
    } else {
      return static_cast<ValueType>(context->r[3 + cursor.ordinal++]);
    }
  }
}

template <typename T>
void WriteHookArg(PPCContext* context, HookArgCursor& cursor, T value) {
  using ValueType = HookTypeT<T>;
  if constexpr (IsFloatingHookType<ValueType>) {
    assert_true(cursor.ordinal < 8);
    assert_true(cursor.fpr < 13);
    ++cursor.ordinal;
    context->f[1 + cursor.fpr++] = static_cast<double>(value);
  } else {
    static_assert(IsIntegerHookType<ValueType>,
                  "Function hooks currently support integer, enum, float and "
                  "double scalar arguments.");
    assert_true(cursor.ordinal < 8);
    if constexpr (std::is_enum_v<ValueType>) {
      using Underlying = std::underlying_type_t<ValueType>;
      if constexpr (std::is_signed_v<Underlying>) {
        context->r[3 + cursor.ordinal++] = static_cast<uint64_t>(
            static_cast<int64_t>(static_cast<Underlying>(value)));
      } else {
        context->r[3 + cursor.ordinal++] =
            static_cast<uint64_t>(static_cast<Underlying>(value));
      }
    } else if constexpr (std::is_signed_v<ValueType>) {
      context->r[3 + cursor.ordinal++] =
          static_cast<uint64_t>(static_cast<int64_t>(value));
    } else {
      context->r[3 + cursor.ordinal++] = static_cast<uint64_t>(value);
    }
  }
}

template <typename... Args>
std::tuple<HookTypeT<Args>...> ReadHookArgs(PPCContext* context) {
  HookArgCursor cursor;
  return std::tuple<HookTypeT<Args>...>{ReadHookArg<Args>(context, cursor)...};
}

template <typename... Args>
void WriteHookArgs(PPCContext* context, Args... args) {
  HookArgCursor cursor;
  (WriteHookArg(context, cursor, args), ...);
}

template <typename Ret>
HookTypeT<Ret> ReadHookReturn(PPCContext* context) {
  using ValueType = HookTypeT<Ret>;
  if constexpr (IsFloatingHookType<ValueType>) {
    return static_cast<ValueType>(context->f[1]);
  } else {
    static_assert(IsIntegerHookType<ValueType>,
                  "Function hooks currently support integer, enum, float, "
                  "double and void return values.");
    if constexpr (std::is_enum_v<ValueType>) {
      using Underlying = std::underlying_type_t<ValueType>;
      return static_cast<ValueType>(static_cast<Underlying>(context->r[3]));
    } else {
      return static_cast<ValueType>(context->r[3]);
    }
  }
}

template <typename Ret>
void WriteHookReturn(PPCContext* context, Ret value) {
  using ValueType = HookTypeT<Ret>;
  if constexpr (IsFloatingHookType<ValueType>) {
    context->f[1] = static_cast<double>(value);
  } else {
    static_assert(IsIntegerHookType<ValueType>,
                  "Function hooks currently support integer, enum, float, "
                  "double and void return values.");
    if constexpr (std::is_enum_v<ValueType>) {
      using Underlying = std::underlying_type_t<ValueType>;
      if constexpr (std::is_signed_v<Underlying>) {
        context->r[3] = static_cast<uint64_t>(
            static_cast<int64_t>(static_cast<Underlying>(value)));
      } else {
        context->r[3] = static_cast<uint64_t>(static_cast<Underlying>(value));
      }
    } else if constexpr (std::is_signed_v<ValueType>) {
      context->r[3] = static_cast<uint64_t>(static_cast<int64_t>(value));
    } else {
      context->r[3] = static_cast<uint64_t>(value);
    }
  }
}

template <typename Ret, typename... Args>
class FunctionHookContext {
 public:
  FunctionHookContext(PPCContext* context, uint32_t address)
      : context_(context), address_(address) {}

  PPCContext* ppc_context() const { return context_; }
  uint32_t address() const { return address_; }

  template <typename... CallArgs>
  Ret CallOriginal(CallArgs... args) {
    static_assert(sizeof...(CallArgs) == sizeof...(Args),
                  "CallOriginal must receive the same number of arguments as "
                  "the hooked function signature.");
    PPCContext saved_context = *context_;

    WriteHookArgs(context_, args...);
    BeginFunctionHookBypass(address_);
    ExecuteFunctionHookOriginal(context_, address_);
    EndFunctionHookBypass(address_);

    if constexpr (std::is_void_v<Ret>) {
      *context_ = saved_context;
    } else {
      auto result = ReadHookReturn<Ret>(context_);
      *context_ = saved_context;
      return result;
    }
  }

 private:
  PPCContext* context_;
  uint32_t address_;
};

template <typename Ret, typename... Args>
class FunctionHook final : public FunctionHookBase {
 public:
  using Handler = std::function<Ret(FunctionHookContext<Ret, Args...>&,
                                    HookTypeT<Args>...)>;

  FunctionHook(uint32_t address, Handler handler)
      : FunctionHookBase(address), handler_(std::move(handler)) {}

  void Invoke(PPCContext* context) override {
    context->function_hook_handled = 0;
    if (IsFunctionHookBypassed(address())) {
      return;
    }

    auto args = ReadHookArgs<Args...>(context);
    FunctionHookContext<Ret, Args...> hook_context(context, address());

    if constexpr (std::is_void_v<Ret>) {
      std::apply(
          [&](HookTypeT<Args>... unpacked) {
            handler_(hook_context, unpacked...);
          },
          args);
      context->function_hook_handled = 1;
    } else {
      auto result = std::apply(
          [&](HookTypeT<Args>... unpacked) {
            return handler_(hook_context, unpacked...);
          },
          args);
      WriteHookReturn(context, result);
      context->function_hook_handled = 1;
    }
  }

 private:
  Handler handler_;
};

}  // namespace function_hook

template <typename Ret, typename... Args, typename Handler>
void RegisterFunctionHook(uint32_t address, Handler&& handler) {
  using Hook = function_hook::FunctionHook<Ret, Args...>;
  function_hook::RegisterFunctionHookImpl(
      address,
      std::make_unique<Hook>(
          address, typename Hook::Handler(std::forward<Handler>(handler))));
}

bool HasFunctionHookAt(uint32_t address);
}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_FRONTEND_H_
