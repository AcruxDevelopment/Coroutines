# standard/WaitConditions.h

`#include <acrux/coroutines/standard/WaitConditions.h>` (namespace `acrux::coroutines::standard`)

## Description

Provides `waitForSeconds`, `waitUntil`, `waitWhile`, and `waitFor` — the built-in helpers for describing what a coroutine should wait on. None of these are part of the core module; each is an ordinary function returning a small callable object that satisfies `Coroutine`'s generic "co_yield anything invocable as `bool()`" contract. See `architecture.md` for what this separation means and how to write an equivalent helper of your own.

## Functions

| Function | Description |
|---|---|
| `waitForSeconds(seconds)` | Resumes once `seconds` of real time have elapsed, measured from `std::chrono::steady_clock`. The clock starts on the condition's first evaluation — the first time `CoroutineRunner::update()` checks it after the coroutine suspends — not when `waitForSeconds()` itself was called. |
| `waitFor(handle)` | Resumes once the coroutine referred to by `handle` has finished. Implemented through `CoroutineHandle::isRunning()`, so it behaves the same as `co_yield handle` directly; provided so that waiting on an already-started handle reads consistently with the other functions here. |
| `waitUntil(predicate)` | Resumes once `predicate()` returns `true`. `predicate` must be convertible to `bool()`; lambdas capturing `this` or other coroutine-local values are supported. This is a thin pass-through — `co_yield predicate` directly would behave identically — provided so a call site reads "waitUntil". |
| `waitWhile(predicate)` | Resumes once `predicate()` returns `false`. Implemented as a small wrapper that negates `predicate()`. |

## Notes

`waitWhile`'s negating wrapper adds a small amount of size on top of the predicate it wraps, which counts against `Predicate`'s 32-byte inline storage limit (see `Predicate.md`). In practice this is only relevant for predicates that were already close to that limit.

None of the functions above are referenced by `Coroutine.h` or `CoroutineRunner.h`. A custom wait condition — for example, waiting until a character's health drops below a threshold — is written the same way, as a small callable with `bool operator()() const`, optionally wrapped in a helper function for a readable call site:

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
