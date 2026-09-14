# CoroutineRunner

`#include <acrux/coroutines/CoroutineRunner.h>`

## Description

`CoroutineRunner` owns and advances every coroutine registered with it, using a generational slot map. See the architecture document for the reasoning behind its internal data structures. `CoroutineRunner` is not copyable, and is not `final` — it is designed to be derived from; see "Extending CoroutineRunner" below.

## Members

| Member | Description |
|---|---|
| `CoroutineRunner()` | Becomes the active runner. Only one `CoroutineRunner` is expected to be active at a time; constructing a second while one already exists replaces which one is considered active, without restoring the first when the second is destroyed. |
| `startCoroutine(task)` | Registers `task` and begins advancing it. Reuses a free slot immediately if one is available; otherwise, the task is staged for insertion at the start of the next `update()` call. Returns `INVALID_COROUTINE_HANDLE` if `task` does not hold a valid coroutine frame. |
| `startSingleCoroutine(slotHandle, task)` | Stops whatever coroutine `slotHandle` currently refers to, if any, then starts `task` and stores the new handle in `slotHandle`. Useful for ensuring at most one instance of a given coroutine is running at a time. |
| `isCoroutineRunning(handle) const` | Whether `handle` refers to a coroutine that is still running. |
| `stopCoroutine(handle)` | Stops the coroutine referred to by `handle`, if still running, and resets `handle` to `INVALID_COROUTINE_HANDLE`. Safe to call on an already-invalid or already-stopped handle. |
| `update()` | Advances every running coroutine by one tick: inserts staged coroutines, resumes any whose wait condition is satisfied, then reclaims finished slots. Intended to be called once per frame. |
| `getActiveCount() const` | The number of coroutines currently tracked, including any still staged for insertion. |

## The active runner

`CoroutineRunner` maintains a static pointer to the most recently constructed instance, cleared by its destructor if it was still the active one. This pointer backs the free functions below, as well as `CoroutineHandle::isRunning()`/`stop()`.

## Free functions

| Function | Description |
|---|---|
| `startCoroutine(task)` | Starts `task` on the active `CoroutineRunner`, without suspending the caller. May be called from `main()`, an ordinary function, or from within another running coroutine. Asserts if no `CoroutineRunner` is active. |
| `stopCoroutine(handle)` | Equivalent to `handle.stop()`. |

## Extending CoroutineRunner

`CoroutineRunner` has a virtual destructor and two protected virtual hooks, provided specifically to support subclassing:

| Member | Description |
|---|---|
| `onCoroutineStarted(handle, parent)` | `protected virtual`. Called once a coroutine becomes live in the slot map — immediately, if `startCoroutine()` reused a free slot, or at the start of the next `update()` otherwise. `parent` is whichever coroutine was actually resuming at the moment `handle` was started (via `co_yield childTask()`, or a direct `startCoroutine()` call from inside a coroutine's body), or `INVALID_COROUTINE_HANDLE` if it was started from outside any coroutine. Not called for a task that is stopped before ever becoming live. Default implementation does nothing. |
| `onCoroutineFinished(handle)` | `protected virtual`. Called when a coroutine's slot is reclaimed, whether it finished naturally (in `update()`) or was stopped explicitly (`stopCoroutine()`). Only called for a handle that previously triggered `onCoroutineStarted()`. Default implementation does nothing. |

A derived class typically adds its own `startCoroutine` overload, accepting whatever extra data it wants to associate with a coroutine, and stores that data itself — keyed by `CoroutineHandle`, which is hashable (see `CoroutineHandle.md`). `onCoroutineFinished()` is then overridden to remove the corresponding entry, keeping the side table from accumulating stale data. `CoroutineRunner` itself has no knowledge of, or opinion on, what the extra data represents.

The `parent` argument to `onCoroutineStarted()` makes it straightforward for a coroutine's children to inherit the same data automatically: a subclass looks `parent` up in its own side table, and if found, copies the association across to the new `handle` before returning. This applies equally to a child started with `co_yield childTask()` and to one started with a plain `startCoroutine()` call made directly from inside a coroutine's body — both are visible as `parent` being that coroutine's own handle. An explicit `startCoroutine(task, extraData)` call made from outside any coroutine still overrides whatever `onCoroutineStarted()` would have inherited, since it runs afterward.

Declaring a new `startCoroutine` overload in a derived class hides the base class's `startCoroutine(Coroutine)` overload, per ordinary C++ overload resolution rules. A `using CoroutineRunner::startCoroutine;` declaration in the derived class restores it. See `example.md` for a complete example.

## Notes

`update()` processes coroutines in three phases — staged insertions, then resumption, then reclaiming finished slots — specifically so that starting new coroutines from within a coroutine resumed during the same call never triggers a reallocation of the active slot storage mid-iteration.
