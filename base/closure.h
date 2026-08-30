/**
 * @file closure.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Callback/Bind: OnceClosure/RepeatingClosure + BindOnce/BindRepeating.
 * @version 0.2
 * @date 2026-08-30
 *
 * Semantics:
 * - A member-function binding's first bound argument (the receiver) must be
 *   std::shared_ptr / std::weak_ptr / std::reference_wrapper;
 * - A std::weak_ptr receiver is lock()-ed before invocation; if the object
 *   is gone, the call is silently skipped (such bindings must return void);
 * - A std::weak_ptr in any other position is passed through untouched —
 *   the callable decides when to lock().
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace lpa {

template <typename Signature>
class OnceClosure;

template <typename Signature>
class RepeatingClosure;

// ============================================================================
// OnceClosure — move-only wrapper around std::move_only_function
// ============================================================================

template <typename R, typename... Params>
class OnceClosure<R(Params...)> final {
 public:
  using Return = R;
  using Signature = R(Params...);
  using FnOnce = std::move_only_function<Signature>;

  constexpr OnceClosure() noexcept = default;
  explicit OnceClosure(FnOnce fn_once) : fn_once_(std::move(fn_once)) {}

  OnceClosure(std::nullptr_t) = delete;
  OnceClosure(OnceClosure const&) = delete;
  OnceClosure& operator=(OnceClosure const&) = delete;
  OnceClosure(OnceClosure&&) noexcept = default;
  OnceClosure& operator=(OnceClosure&&) noexcept = default;
  ~OnceClosure() = default;

  Return Run(Params...) & = delete;

  Return Run(Params... params) && {
    assert(fn_once_ && "Calling Run() on an already-consumed OnceClosure");
    return std::exchange(fn_once_, FnOnce{})(std::forward<Params>(params)...);
  }

  bool is_null() const noexcept { return !fn_once_; }
  void Reset() noexcept { fn_once_ = FnOnce{}; }
  explicit operator bool() const noexcept { return !!fn_once_; }

  // Chain: once → once (consumes this closure).
  // invoke_result_t uses FDecayed&: `next` is a captured member, so the
  // actual call is an lvalue call. The deduced value category must match
  // the actual one, otherwise a ref-qualified operator() would be wrongly
  // accepted or rejected.
  template <typename F>
  [[nodiscard]] auto Then(F&& f) && {
    using FDecayed = std::decay_t<F>;
    if constexpr (std::is_void_v<Return>) {
      using NextReturn = std::invoke_result_t<FDecayed &>;
      return OnceClosure<NextReturn(Params...)>(
          [first = std::move(*this),
           next = std::forward<F>(f)](Params... params) mutable -> NextReturn {
            std::move(first).Run(std::forward<Params>(params)...);
            if constexpr (std::is_void_v<NextReturn>) {
              next();
            } else {
              return next();
            }
          });
    } else {
      using NextReturn = std::invoke_result_t<FDecayed &, Return>;
      return OnceClosure<NextReturn(Params...)>(
          [first = std::move(*this),
           next = std::forward<F>(f)](Params... params) mutable -> NextReturn {
            return next(std::move(first).Run(std::forward<Params>(params)...));
          });
    }
  }

 private:
  FnOnce fn_once_;
};

// ============================================================================
// RepeatingClosure — copyable wrapper around std::function
// ============================================================================

template <typename R, typename... Params>
class RepeatingClosure<R(Params...)> final {
 public:
  using Return = R;
  using Signature = R(Params...);
  using Fn = std::function<Signature>;

  constexpr RepeatingClosure() noexcept = default;
  explicit RepeatingClosure(Fn fn) : fn_(std::move(fn)) {}

  RepeatingClosure(RepeatingClosure const&) = default;
  RepeatingClosure& operator=(RepeatingClosure const&) = default;
  RepeatingClosure(RepeatingClosure&&) noexcept = default;
  RepeatingClosure& operator=(RepeatingClosure&&) noexcept = default;
  ~RepeatingClosure() = default;

  Return Run(Params... params) & {
    assert(fn_ && "Calling Run() on a null RepeatingClosure");
    return fn_(std::forward<Params>(params)...);
  }

  Return Run(Params... params) && {
    assert(fn_ && "Calling Run() on a null RepeatingClosure");
    return std::exchange(fn_, Fn{})(std::forward<Params>(params)...);
  }

  bool is_null() const noexcept { return !fn_; }
  void Reset() noexcept { fn_ = Fn{}; }
  explicit operator bool() const noexcept { return !!fn_; }

  template <typename F>
  [[nodiscard]] auto Then(F&& f) & {
    return std::move(RepeatingClosure{*this}).Then(std::forward<F>(f));
  }

  // Chain: repeating → repeating (r-value) — moves this closure.
  // F must be copyable: the chained lambda is stored in std::function
  // (copy semantics). A move-only F would explode into template errors at
  // the std::function construction site — intercept it early.
  template <typename F>
  [[nodiscard]] auto Then(F&& f) && {
    using FDecayed = std::decay_t<F>;
    static_assert(std::is_copy_constructible_v<FDecayed>,
                  "RepeatingClosure::Then requires a copy-constructible "
                  "continuation (it is stored in std::function). Use "
                  "OnceClosure for move-only continuations.");
    if constexpr (std::is_void_v<Return>) {
      using NextReturn = std::invoke_result_t<FDecayed &>;
      return RepeatingClosure<NextReturn(Params...)>(
          [original = std::move(*this),
           next = std::forward<F>(f)](Params... params) mutable -> NextReturn {
            original.Run(std::forward<Params>(params)...);
            if constexpr (std::is_void_v<NextReturn>) {
              next();
            } else {
              return next();
            }
          });
    } else {
      using NextReturn = std::invoke_result_t<FDecayed &, Return>;
      return RepeatingClosure<NextReturn(Params...)>(
          [original = std::move(*this),
           next = std::forward<F>(f)](Params... params) mutable -> NextReturn {
            return next(original.Run(std::forward<Params>(params)...));
          });
    }
  }

  // Convert to OnceClosure (consumes this).
  operator OnceClosure<R(Params...)>() && {
    return OnceClosure<R(Params...)>(std::move(fn_));
  }

 private:
  Fn fn_;
};

namespace detail {

// ─── Category enums ─────────────────────────────────────────────────

enum class CallableCategory {
  kFn,      // Regular functions, function pointers, or references.
  kMemFn,   // Pointer to member functions (PMF).
  kFunctor, // Classes with operator(), including Lambdas.
};

enum class ClosureCategory {
  kOnce,
  kRepeating,
};

template <typename...>
inline constexpr bool always_false_v = false;

// ─── TypeList (minimal: only what Bind needs) ───────────────────────

template <typename... Types>
struct TypeList {
  static constexpr std::size_t size = sizeof...(Types);
};

template <typename List, std::size_t N>
struct DropImpl;

template <typename Head, typename... Tail, std::size_t N>
  requires(N > 0u)
struct DropImpl<TypeList<Head, Tail...>, N>
    : DropImpl<TypeList<Tail...>, N - 1u> {};

template <typename List>
struct DropImpl<List, 0u> {
  using type = List;
};

template <typename List, std::size_t N>
  requires(N <= List::size)
using Drop = typename DropImpl<List, N>::type;

template <typename List, typename Return>
struct MakeSignatureImpl;

template <typename Return, typename... Params>
struct MakeSignatureImpl<TypeList<Params...>, Return> {
  using type = Return(Params...);
};

template <typename List, typename Return>
using MakeSignature = typename MakeSignatureImpl<List, Return>::type;

template <typename List, std::size_t N>
struct AtImpl;

template <typename Head, typename... Tail, std::size_t N>
  requires(N > 0u)
struct AtImpl<TypeList<Head, Tail...>, N> : AtImpl<TypeList<Tail...>, N - 1u> {
};

template <typename Head, typename... Tail>
struct AtImpl<TypeList<Head, Tail...>, 0u> {
  using type = Head;
};

template <typename List, std::size_t N>
  requires(N < List::size)
using At = typename AtImpl<List, N>::type;

// ─── CallableTraits ─────────────────────────────────────────────────

template <typename Callable>
struct CallableTraits {
  static_assert(always_false_v<Callable>,
                "The provided type is not a supported callable (e.g., "
                "overloaded functors or generic lambdas).");
};

template <typename R, typename... Ps>
struct CallableTraits<R (*)(Ps...)> {
  using Return = R;
  using ParameterList = TypeList<Ps...>;
  using CanonicalParameterList = TypeList<Ps...>;

  static constexpr CallableCategory callable_category = CallableCategory::kFn;
};

template <typename R, typename... Ps>
struct CallableTraits<R(Ps...)> : CallableTraits<R (*)(Ps...)> {};

template <typename R, typename... Ps>
struct CallableTraits<R (&)(Ps...)> : CallableTraits<R (*)(Ps...)> {};

template <typename R, typename... Ps>
struct CallableTraits<R (*)(Ps...) noexcept> : CallableTraits<R (*)(Ps...)> {};

template <typename R, typename... Ps>
struct CallableTraits<R(Ps...) noexcept>
    : CallableTraits<R (*)(Ps...) noexcept> {};

template <typename R, typename... Ps>
struct CallableTraits<R (&)(Ps...) noexcept>
    : CallableTraits<R (*)(Ps...) noexcept> {};

// Shared PMF traits. Volatile member functions are not supported.
template <typename R, typename C, typename... Ps>
struct MemFnTraits {
  using Return = R;
  using Class = C;
  using ParameterList = TypeList<Ps...>;
  using CanonicalParameterList = TypeList<C*, Ps...>;

  static constexpr CallableCategory callable_category =
      CallableCategory::kMemFn;
};

// {none, const} × {none, &, &&} × {plain, noexcept} = 12
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...)> : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) const> : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) &> : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) const&> : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) &&> : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) const&&> : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) noexcept> : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) const noexcept>
    : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) & noexcept> : MemFnTraits<R, C, Ps...> {
};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) const& noexcept>
    : MemFnTraits<R, C, Ps...> {};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) && noexcept> : MemFnTraits<R, C, Ps...> {
};
template <typename R, typename C, typename... Ps>
struct CallableTraits<R (C::*)(Ps...) const&& noexcept>
    : MemFnTraits<R, C, Ps...> {};

// Functor Traits: Resolves classes via their operator() address.
// Only non-overloaded and non-generic functors are supported.
template <typename Functor>
  requires requires { &std::remove_reference_t<Functor>::operator(); }
struct CallableTraits<Functor> {
 private:
  using PMFTraits =
      CallableTraits<decltype(&std::remove_reference_t<Functor>::operator())>;

 public:
  using Return = typename PMFTraits::Return;
  using ParameterList = typename PMFTraits::ParameterList;
  using CanonicalParameterList = typename PMFTraits::ParameterList;

  static constexpr CallableCategory callable_category =
      CallableCategory::kFunctor;
};

// ─── Template Specialization Detector ───────────────────────────────

template <typename T, template <typename...> class Template>
struct is_specialization_of : std::false_type {};

template <template <typename...> class Template, typename... Args>
struct is_specialization_of<Template<Args...>, Template> : std::true_type {};

template <typename T, template <typename...> class Template>
inline constexpr bool is_specialization_of_v =
    is_specialization_of<T, Template>::value;

// ─── Wrapper detection & unwrapping ─────────────────────────────────
// Standard-library wrappers only. std::weak_ptr is deliberately absent:
// it has no get() to unwrap, so as a regular bound argument it passes
// through untouched; only in the PMF receiver position does it take the
// lock() path (see InvokeWithWeakReceiver).

template <typename T>
struct IsWrapper : std::false_type {};
template <typename T>
struct IsWrapper<std::shared_ptr<T>> : std::true_type {};
template <typename T>
struct IsWrapper<std::reference_wrapper<T>> : std::true_type {};

template <typename T>
inline constexpr bool IsWrapperV = IsWrapper<T>::value;

template <typename T>
struct UnwrapTraits {
  using type = T;
};
template <typename T>
struct UnwrapTraits<std::shared_ptr<T>> {
  using type = T*;
};
template <typename T>
struct UnwrapTraits<std::reference_wrapper<T>> {
  using type = T&;
};

// Type-level view of a std::weak_ptr receiver: at runtime it is not
// unwrapped but lock()-ed; T* approximates what std::invoke receives.
template <typename Stored>
struct LockedWeakPtrArg;

template <typename T>
struct LockedWeakPtrArg<std::weak_ptr<T>> {
  using type = T*;
};

template <bool kLockedWeakReceiver, typename Stored, typename Param, bool kOnce>
struct EffectiveBoundArgImpl {
  using type = std::conditional_t<
      !IsWrapperV<std::decay_t<Stored>> ||
          std::is_same_v<std::decay_t<Stored>, std::decay_t<Param>>,
      std::conditional_t<kOnce, Stored&&, Stored&>,
      typename UnwrapTraits<std::decay_t<Stored>>::type>;
};

template <typename Stored, typename Param, bool kOnce>
struct EffectiveBoundArgImpl<true, Stored, Param, kOnce> {
  using type = typename LockedWeakPtrArg<std::decay_t<Stored>>::type;
};

template <typename Stored,
          typename Param,
          bool kOnce,
          bool kLockedWeakReceiver>
using EffectiveBoundArg =
    typename EffectiveBoundArgImpl<kLockedWeakReceiver, Stored, Param,
                                   kOnce>::type;

// A PMF receiver's canonical parameter is written as C*, but unwrapping a
// reference_wrapper yields C& — which std::invoke accepts natively —
// so T& is checked against C& in that case.
template <typename Effective, typename Param>
inline constexpr bool ArgConvertibleV = std::is_convertible_v<Effective, Param>;

template <typename T, typename C>
  requires(!std::is_pointer_v<T>)
inline constexpr bool ArgConvertibleV<T&, C*> = std::is_convertible_v<T&, C&>;

// Single unwrapping entry point. If the callable's corresponding parameter
// is the wrapper type itself, the wrapper passes through untouched.
template <typename Param, typename Stored>
decltype(auto) Unwrap(Stored&& stored) {
  using D = std::decay_t<Stored>;
  if constexpr (!IsWrapperV<D> || std::is_same_v<D, std::decay_t<Param>>) {
    return std::forward<Stored>(stored);
  } else {
    // shared_ptr::get() → T*；reference_wrapper::get() → T&
    return stored.get();
  }
}

// ─── Invoke ─────────────────────────────────────────────────────────

template <typename Callable,
          typename BoundTuple,
          std::size_t... I,
          typename CanonicalList,
          typename... Unbound>
decltype(auto) Invoke(Callable&& callable,
                      BoundTuple&& bound_tuple,
                      std::index_sequence<I...>,
                      CanonicalList,
                      Unbound&&... unbound) {
  return std::invoke(
      std::forward<Callable>(callable),
      Unwrap<At<CanonicalList, I>>(
          std::get<I>(std::forward<BoundTuple>(bound_tuple)))...,
      std::forward<Unbound>(unbound)...);
}

// ─── std::weak_ptr receiver（PMF only）─────────────────────────────
// When the receiver is a std::weak_ptr: lock() into a temporary
// shared_ptr before the call; if the object is gone, skip silently
// (such bindings must return void — enforced by BindValidator).

template <typename Callable, typename BoundTuple>
inline constexpr bool HasWeakReceiver = [] {
  using Traits = CallableTraits<std::decay_t<Callable>>;
  if constexpr (Traits::callable_category == CallableCategory::kMemFn &&
                std::tuple_size_v<std::decay_t<BoundTuple>> > 0) {
    using Receiver = std::tuple_element_t<0, std::decay_t<BoundTuple>>;
    return is_specialization_of_v<Receiver, std::weak_ptr>;
  }
  return false;
}();

template <typename Callable,
          typename LockedPtr,
          typename BoundTuple,
          std::size_t... J,
          typename CanonicalList,
          typename... Unbound>
decltype(auto) InvokeWithWeakReceiverImpl(Callable&& callable,
                                          LockedPtr&& locked,
                                          BoundTuple&& bound_tuple,
                                          std::index_sequence<J...>,
                                          CanonicalList,
                                          Unbound&&... unbound) {
  // locked is a shared_ptr<T>; std::invoke natively supports
  // smart-pointer receivers.
  return std::invoke(
      std::forward<Callable>(callable), std::forward<LockedPtr>(locked),
      Unwrap<At<CanonicalList, 1 + J>>(
          std::get<1 + J>(std::forward<BoundTuple>(bound_tuple)))...,
      std::forward<Unbound>(unbound)...);
}

template <typename Callable,
          typename BoundTuple,
          std::size_t... I,
          typename CanonicalList,
          typename... Unbound>
decltype(auto) InvokeWithWeakReceiver(Callable&& callable,
                                      BoundTuple&& bound_tuple,
                                      std::index_sequence<I...>,
                                      CanonicalList,
                                      Unbound&&... unbound) {
  auto locked = std::get<0>(bound_tuple).lock();
  if (!locked) {
    return;  // R is guaranteed to be void by BindValidator.
  }
  constexpr std::size_t kExtraArgs = (sizeof...(I) > 0) ? sizeof...(I) - 1 : 0;
  return InvokeWithWeakReceiverImpl(
      std::forward<Callable>(callable), std::move(locked),
      std::forward<BoundTuple>(bound_tuple),
      std::make_index_sequence<kExtraArgs>{}, CanonicalList{},
      std::forward<Unbound>(unbound)...);
}

// ─── InvokeGuarded ──────────────────────────────────────────────────
// Dispatch point: a std::weak_ptr receiver takes the lock() path;
// everything else is invoked directly.

template <typename Callable,
          typename BoundTuple,
          std::size_t... I,
          typename CanonicalList,
          typename... Unbound>
decltype(auto) InvokeGuarded(Callable&& c,
                             BoundTuple&& t,
                             std::index_sequence<I...>,
                             CanonicalList,
                             Unbound&&... unbound) {
  if constexpr (HasWeakReceiver<Callable, BoundTuple>) {
    return InvokeWithWeakReceiver(
        std::forward<Callable>(c), std::forward<BoundTuple>(t),
        std::index_sequence<I...>{}, CanonicalList{},
        std::forward<Unbound>(unbound)...);
  } else {
    return Invoke(std::forward<Callable>(c), std::forward<BoundTuple>(t),
                  std::index_sequence<I...>{}, CanonicalList{},
                  std::forward<Unbound>(unbound)...);
  }
}

// ─── InvokerGenerator ───────────────────────────────────────────────

template <typename Signature>
struct InvokerGenerator;

template <typename R, typename... Params>
struct InvokerGenerator<R(Params...)> {
  template <typename Callable,
            typename BoundTuple,
            typename CanonicalList,
            std::size_t... Indices>
  static auto CreateOnce(Callable&& c,
                         BoundTuple&& t,
                         std::index_sequence<Indices...>,
                         CanonicalList) {
    return [c = std::forward<Callable>(c),
            t = std::forward<BoundTuple>(t)](Params... params) mutable -> R {
      return InvokeGuarded(std::move(c), std::move(t),
                           std::index_sequence<Indices...>{}, CanonicalList{},
                           std::forward<Params>(params)...);
    };
  }

  template <typename Callable,
            typename BoundTuple,
            typename CanonicalList,
            std::size_t... Indices>
  static auto CreateRepeating(Callable&& c,
                              BoundTuple&& t,
                              std::index_sequence<Indices...>,
                              CanonicalList) {
    return [c = std::forward<Callable>(c),
            t = std::forward<BoundTuple>(t)](Params... params) mutable -> R {
      return InvokeGuarded(c, t, std::index_sequence<Indices...>{},
                           CanonicalList{}, std::forward<Params>(params)...);
    };
  }
};

// ─── BindValidator ──────────────────────────────────────────────────

template <typename T>
struct IsMoveOnly : std::bool_constant<!std::is_copy_constructible_v<T> &&
                                       std::is_move_constructible_v<T>> {};

template <bool kOnce,
          bool kWeakLock,
          typename CanonicalList,
          typename StoredTuple,
          std::size_t... I>
constexpr bool BoundArgsConvertible(std::index_sequence<I...>) {
  return (ArgConvertibleV<
              EffectiveBoundArg<std::tuple_element_t<I, StoredTuple>,
                                At<CanonicalList, I>, kOnce,
                                kWeakLock && (I == 0)>,
              At<CanonicalList, I>> && ...);
}

template <ClosureCategory closure_category,
          typename Traits,
          typename Callable,
          typename... BoundArgs>
struct BindValidator {
  using CanonicalParams = typename Traits::CanonicalParameterList;

  // 1. Arity Check: Bound args cannot exceed callable params.
  static constexpr bool kArityOk =
      sizeof...(BoundArgs) <= CanonicalParams::size;

  // 2. Member function bindings require a receiver (first bound arg).
  static constexpr bool kMemFnNeedsReceiver =
      (Traits::callable_category != CallableCategory::kMemFn) ||
      (sizeof...(BoundArgs) > 0);

  // 3. Move-Only types in RepeatingClosure.
  static constexpr bool kNoMoveOnlyInRepeating =
      (closure_category == ClosureCategory::kRepeating)
          ? !(IsMoveOnly<BoundArgs>::value || ...)
          : true;

  // 4. Receiver validation: only the three standard wrappers are
  //    accepted; raw pointers, values, and custom wrappers are rejected.
  static constexpr bool kReceiverCheck = [] {
    if constexpr (Traits::callable_category == CallableCategory::kMemFn &&
                  sizeof...(BoundArgs) > 0) {
      using Receiver =
          std::tuple_element_t<0, std::tuple<std::decay_t<BoundArgs>...>>;
      return is_specialization_of_v<Receiver, std::shared_ptr> ||
             is_specialization_of_v<Receiver, std::weak_ptr> ||
             is_specialization_of_v<Receiver, std::reference_wrapper>;
    }
    return true;
  }();

  // 5. A std::weak_ptr receiver requires a void return: lock() may fail,
  //    in which case the call is silently skipped, and a non-void return
  //    would yield a misleading default-constructed value.
  static constexpr bool kHasWeakReceiver = [] {
    if constexpr (Traits::callable_category == CallableCategory::kMemFn &&
                  sizeof...(BoundArgs) > 0) {
      using Receiver =
          std::tuple_element_t<0, std::tuple<std::decay_t<BoundArgs>...>>;
      return is_specialization_of_v<Receiver, std::weak_ptr>;
    }
    return false;
  }();

  static constexpr bool kWeakVoidReturn =
      !kHasWeakReceiver || std::is_void_v<typename Traits::Return>;

  // 6. Bound args must be convertible to their corresponding parameters.
  static constexpr bool kBoundArgsConvertible =
      BoundArgsConvertible<closure_category == ClosureCategory::kOnce,
                           kHasWeakReceiver, CanonicalParams,
                           std::tuple<std::decay_t<BoundArgs>...>>(
          std::make_index_sequence<kArityOk ? sizeof...(BoundArgs) : 0>{});

  static constexpr bool value = kArityOk && kMemFnNeedsReceiver &&
                                kNoMoveOnlyInRepeating && kReceiverCheck &&
                                kWeakVoidReturn && kBoundArgsConvertible;
};

// ─── BindHelper ─────────────────────────────────────────────────────

template <ClosureCategory closure_category>
struct BindHelper {
  template <typename Callable, typename... BoundArgs>
  static auto Bind(Callable&& callable, BoundArgs&&... args) {
    using Traits = CallableTraits<std::decay_t<Callable>>;
    using Return = typename Traits::Return;
    using ParamsList = typename Traits::CanonicalParameterList;

    using Validator = BindValidator<closure_category, Traits,
                                    std::decay_t<Callable>, BoundArgs...>;

    static_assert(Validator::kArityOk, "Too many bound arguments provided.");
    static_assert(Validator::kMemFnNeedsReceiver,
                  "Binding a member function requires a receiver as the "
                  "first bound argument (e.g., std::ref(obj) or a smart "
                  "pointer).");
    static_assert(Validator::kNoMoveOnlyInRepeating,
                  "RepeatingClosure cannot bind move-only types (e.g., "
                  "std::unique_ptr). Use OnceClosure instead.");
    static_assert(
        Validator::kReceiverCheck,
        "Member function receiver must be std::shared_ptr / std::weak_ptr / "
        "std::reference_wrapper. Raw pointers and references are forbidden.");
    static_assert(
        Validator::kWeakVoidReturn,
        "std::weak_ptr receiver requires void return type. "
        "std::weak_ptr::lock() may fail if the object is destroyed, "
        "so non-void returns would give misleading default-constructed "
        "values.");
    static_assert(Validator::kBoundArgsConvertible,
                  "A bound argument is not convertible to the corresponding "
                  "parameter type (check the types and order of the bound "
                  "arguments).");

    if constexpr (Validator::value) {
      constexpr std::size_t kBoundArity = sizeof...(BoundArgs);
      using UnboundList = Drop<ParamsList, kBoundArity>;
      using Signature = MakeSignature<UnboundList, Return>;

      using TupleType = std::tuple<std::decay_t<BoundArgs>...>;
      TupleType bound_tuple{std::forward<BoundArgs>(args)...};

      using Generator = InvokerGenerator<Signature>;

      if constexpr (closure_category == ClosureCategory::kOnce) {
        auto invoker = Generator::CreateOnce(
            std::forward<Callable>(callable), std::move(bound_tuple),
            std::make_index_sequence<kBoundArity>{}, ParamsList{});
        return OnceClosure<Signature>(std::move(invoker));
      } else {
        auto invoker = Generator::CreateRepeating(
            std::forward<Callable>(callable), std::move(bound_tuple),
            std::make_index_sequence<kBoundArity>{}, ParamsList{});
        return RepeatingClosure<Signature>(std::move(invoker));
      }
    } else {
      constexpr std::size_t kSafeArity =
          Validator::kArityOk ? sizeof...(BoundArgs) : 0;
      using BestEffortSignature =
          MakeSignature<Drop<ParamsList, kSafeArity>, Return>;
      if constexpr (closure_category == ClosureCategory::kOnce) {
        return OnceClosure<BestEffortSignature>{};
      } else {
        return RepeatingClosure<BestEffortSignature>{};
      }
    }
  }
};

} // namespace detail

// ============================================================================
// Public entry points
// ============================================================================

template <typename Callable, typename... BoundArguments>
[[nodiscard]] inline auto BindOnce(Callable&& callable,
                                   BoundArguments&&... bound_arguments) {
  return detail::BindHelper<detail::ClosureCategory::kOnce>::Bind(
      std::forward<Callable>(callable),
      std::forward<BoundArguments>(bound_arguments)...);
}

template <typename Callable, typename... BoundArguments>
[[nodiscard]] inline auto BindRepeating(
    Callable&& callable, BoundArguments&&... bound_arguments) {
  return detail::BindHelper<detail::ClosureCategory::kRepeating>::Bind(
      std::forward<Callable>(callable),
      std::forward<BoundArguments>(bound_arguments)...);
}

} // namespace lpa
