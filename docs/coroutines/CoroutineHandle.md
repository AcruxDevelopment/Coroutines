# CoroutineHandle

`#include <acrux/coroutines/CoroutineHandle.h>`

## Description

`CoroutineHandle` identifies a coroutine registered with a `CoroutineRunner`. It is a small, trivially-copyable value (a single `uint64_t` internally) rather than a raw integer, so it cannot be confused with an unrelated value, and supports `handle.stop()`/`handle.isRunning()` directly.

## Members

| Member | Description |
|---|---|
| `operator bool() const` | `explicit`. `true` unless this handle equals `INVALID_COROUTINE_HANDLE`. Does not check whether the coroutine is still running; use `isRunning()` for that. |
| `isRunning() const` | Equivalent to calling `isCoroutineRunning()` on the active `CoroutineRunner`. Asserts if no `CoroutineRunner` is active. |
| `stop()` | Stops the coroutine and resets this handle to `INVALID_COROUTINE_HANDLE`. Safe to call on an already-invalid handle. Asserts if no `CoroutineRunner` is active. |
| `operator==` / `operator!=` | Compares the underlying id. |
| `id() const` | The handle's raw packed id, exposed so `CoroutineHandle` can be used as a key in `std::unordered_map`/`std::unordered_set`. Not meant to be decoded into its index/generation components directly. |

## `INVALID_COROUTINE_HANDLE`

An `inline constexpr CoroutineHandle` representing "no coroutine." Both `CoroutineHandle{}` and `CoroutineHandle()` produce this value.

## `std::hash<CoroutineHandle>`

A specialization is provided, based on `id()`, so `CoroutineHandle` can be used directly as a key in an unordered container:

```cpp
std::unordered_map<CoroutineHandle, EntityId> owners;
```

This is the mechanism a `CoroutineRunner` subclass is expected to use for tracking its own per-coroutine data — see `CoroutineRunner.md` and `example.md`.

## Notes

`CoroutineHandle` can only be constructed from a raw id by `CoroutineRunner`, which is declared a friend for this purpose; application code cannot construct an arbitrary handle. `isRunning()`/`stop()` are declared here but defined in `CoroutineRunner.h`, since they require `CoroutineRunner` to be a complete type.
