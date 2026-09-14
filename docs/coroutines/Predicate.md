# Predicate

`#include <acrux/coroutines/Predicate.h>`

## Description

`Predicate` is a small-buffer-optimized, type-erased `bool()` callable. Any callable that fits within its inline buffer is stored directly, without a heap allocation — covering the common case of a lambda capturing `this` and a small number of local values.

It is used directly by `Coroutine::promise_type` to store whatever condition the currently suspended coroutine is waiting on (see `Coroutine.md`), and by the helpers in `standard/WaitConditions.h`. Unlike those helpers, `Predicate` is part of the core module — the scheduler's ability to wait on an arbitrary condition depends on it.

```cpp
Predicate p1 = [this]{ return this->targetInRange; };
Predicate p2 = [hp = enemy->hp]{ return hp <= 0; };
```

## Members

| Member | Description |
|---|---|
| `Predicate()` | Constructs an empty predicate. `operator bool()` returns `false`; calling `operator()` asserts. |
| `Predicate(f)` | Constructs from any callable `f` convertible to `bool()`. Requires `sizeof(std::decay_t<F>) <= 32` bytes and `alignof(std::decay_t<F>) <= alignof(std::max_align_t)`; both are enforced with `static_assert`. A lambda exceeding this size should capture by pointer or reference instead of by value. |
| `operator()() const` | Invokes the stored callable. Asserts if the predicate is empty. |
| `operator bool() const` | `explicit`. `true` if a callable is currently stored. |
| Move construction / assignment | Transfers the stored callable. |
| Copy construction / assignment | Deleted. `Predicate` is move-only. |

## Notes

The 32-byte inline buffer size is fixed at compile time and is not configurable per instance. Storage larger than this is not supported; there is no fallback to heap allocation. `Predicate` is implemented with a small, hand-written function-pointer table rather than `std::function`, specifically to guarantee no heap allocation for anything that fits.
