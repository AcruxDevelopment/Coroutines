# Coroutines

The Coroutines module provides a Unity-style coroutine system built on C++20's `co_await`/`co_yield`. A function returning `Coroutine` may suspend itself with `co_yield` and be resumed later — after a number of frames, after a delay, or once a condition becomes true — by a central `CoroutineRunner`.

## Components

| Location | Role |
|---|---|
| `Coroutine.h` | The return type of a coroutine function, along with its `promise_type`. Required for `Coroutine foo() { co_yield ...; }` to compile. |
| `CoroutineHandle.h` | A lightweight, opaque value identifying a running coroutine. |
| `CoroutineRunner.h` | Owns and advances every running coroutine. |
| `Predicate.h` | A small-buffer-optimized, type-erased `bool()` callable, used internally by the scheduler and by the wait conditions below. |
| `standard/WaitConditions.h` | `waitForSeconds`, `waitUntil`, `waitWhile`, `waitFor` — the built-in helpers for describing what to wait on. |

## Core versus standard

`Coroutine.h` and `CoroutineRunner.h` form the core of the module. They know how to suspend and resume a coroutine, and how to wait on exactly three things:

- the next frame (`co_yield nullptr`)
- another coroutine finishing (`co_yield someHandle`, or `co_yield someOtherCoroutine()` to start and wait on a child)
- **any callable that returns `bool`** (`co_yield someCondition`)

That last case is deliberately open-ended. The core has no concept of "waiting for seconds" or "waiting for a condition" as distinct kinds of instruction — it only knows how to call something and check whether it returned `true`.

`standard/WaitConditions.h` is built entirely on top of that public interface. `waitForSeconds`, `waitUntil`, `waitWhile`, and `waitFor` are ordinary functions that return small callable objects satisfying the core's `bool()` contract. Neither `Coroutine.h` nor `CoroutineRunner.h` includes this header, or knows it exists. It is kept in its own `standard/` directory specifically to make this separation visible: the core scheduler does not depend on it, and it can be replaced or extended without touching the scheduler at all.

Writing a custom wait condition follows the same one-line pattern used internally:

```cpp
class UntilHealthBelow
{
public:
    UntilHealthBelow(const Character& c, int threshold) : m_character(c), m_threshold(threshold) {}
    bool operator()() const { return m_character.hp < m_threshold; }

private:
    const Character& m_character;
    int m_threshold;
};

co_yield UntilHealthBelow(enemy, 10);
```

No changes to the core module, and no registration step, are required.

## Execution model

A function returning `Coroutine` behaves like an ordinary function at the call site — it may take arguments, and may be a free function, a static function, or a non-static member function. Calling it does not run its body. It constructs a suspended coroutine frame, since `initial_suspend()` always suspends. The body does not begin executing until the coroutine is registered with a `CoroutineRunner`, either directly through `startCoroutine()` or implicitly through `co_yield childTask`, and `CoroutineRunner::update()` is called — typically once per frame.

## CoroutineRunner internals

`CoroutineRunner` stores coroutines in a generational slot map rather than a plain list:

- A `CoroutineHandle` packs a 32-bit slot index and a 32-bit generation counter into a single `uint64_t`. Looking up a handle is a direct index into a vector, and the generation check rejects a handle whose slot has since been reused by a different coroutine.
- Freed slots are placed on a free list and reused before the underlying storage grows.
- Growing the underlying storage is deferred to the start of the next `update()` call. During `update()`, references into the slot array remain valid while coroutines are resumed, and a resumed coroutine may start new coroutines in the same tick. Reallocating the vector during that loop would invalidate those references, so newly allocated slots are staged and only inserted at the beginning of the following `update()`. Reusing an existing free slot has no such restriction and happens immediately.
- A scratch buffer used to track finished slots each tick is retained and cleared between calls, rather than being reallocated.
- The active coroutine count is tracked incrementally rather than recomputed from the slot array.

## The active runner

`CoroutineRunner` maintains a static pointer to the most recently constructed instance. Only one `CoroutineRunner` is expected to be active at a time. This pointer backs the free functions `startCoroutine()`/`stopCoroutine()`, as well as `CoroutineHandle::isRunning()`/`stop()`, allowing them to be called from anywhere — including from within another running coroutine — without passing a `CoroutineRunner&` explicitly. Each of these asserts if no `CoroutineRunner` currently exists.

## Extending CoroutineRunner

`CoroutineRunner` is not `final`, specifically so it can be derived from. This is the intended way to attach extra data to a coroutine — for example, tagging each coroutine in a game engine with the entity it belongs to.

A derived class adds its own `startCoroutine` overload accepting whatever extra data it needs, and tracks that data itself, keyed by `CoroutineHandle`. `CoroutineHandle` is hashable (`std::hash<CoroutineHandle>` is provided) specifically to support this. Two protected virtual hooks keep that side table in sync automatically:

| Hook | Called |
|---|---|
| `onCoroutineStarted(handle, parent)` | Once a coroutine becomes live in the slot map. `parent` is whichever coroutine was actually resuming at that moment, or `INVALID_COROUTINE_HANDLE` if none was. |
| `onCoroutineFinished(handle)` | Once a coroutine's slot is reclaimed, whether it finished naturally or was stopped explicitly. |

The base class has no opinion on what the extra data represents — that is entirely up to the derived class. `parent` is what allows a coroutine's children to inherit that data automatically: a subclass looks `parent` up in its own side table inside `onCoroutineStarted()`, and copies the association across if found. This applies whether the child was started with `co_yield childTask()` or with a plain `startCoroutine()` call made directly from within a coroutine's body — both cases set `parent` to that coroutine's own handle. See `example.md` for a complete example.

One consequence of ordinary C++ overload resolution to be aware of: declaring a new `startCoroutine` overload in a derived class hides the base class's `startCoroutine(Coroutine)` overload, rather than adding to it. A `using CoroutineRunner::startCoroutine;` declaration in the derived class restores access to it.

## Header organization

`CoroutineHandle::isRunning()`/`stop()` and `Coroutine::promise_type::yield_value(Coroutine&&)` are declared in `CoroutineHandle.h`/`Coroutine.h`, but defined in `CoroutineRunner.h`. Their implementations require `CoroutineRunner` to be a complete type, and `CoroutineRunner.h` is the header that includes both `CoroutineHandle.h` and `Coroutine.h` — including it the other way around would create a circular include.

## Lifetime

A non-static member coroutine captures `this`, in the same way any other closure would. If the owning object is destroyed while the coroutine is still suspended, resuming it afterward accesses freed memory. If there is any possibility of this happening, the coroutine should be stopped explicitly — through `CoroutineRunner::stopCoroutine()` or `handle.stop()` — in the owning object's destructor.
