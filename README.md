# acrux::coroutines

A Unity-style coroutine system for C++20, built on `co_await`/`co_yield`. Header-only.

```cpp
#include <acrux/coroutines/Coroutine.h>
#include <acrux/coroutines/CoroutineRunner.h>
#include <acrux/coroutines/standard/WaitConditions.h>

using namespace acrux::coroutines;
using namespace acrux::coroutines::standard;

Coroutine fadeOut(float& alpha, float delta)
{
    while (alpha > 0)
    {
        alpha -= delta;
        co_yield waitForSeconds(0.1f);
    }
}

int main()
{
    CoroutineRunner runner;
    float alpha = 1.0f;

    CoroutineHandle h = startCoroutine(fadeOut(alpha, 0.1f));

    while (h.isRunning())
    {
        runner.update(); // call once per frame
    }
}
```

## Layout

```
include/acrux/coroutines/
  Coroutine.h              the Coroutine return type + its promise_type
  CoroutineHandle.h         a cheap, hashable value identifying a coroutine
  CoroutineRunner.h         the scheduler
  Predicate.h                small-buffer-optimized type-erased bool() callable
  standard/
    WaitConditions.h         waitForSeconds / waitUntil / waitWhile / waitFor --
                              built entirely on the public core API, and not
                              depended on by anything above
test/coroutines/main.cpp    a runnable example, also the module's test suite
docs/coroutines/             one file per type, plus architecture.md and example.md
```

## Three features worth knowing about up front

**The scheduler doesn't know about `waitForSeconds`/`waitUntil`/etc.** `Coroutine` accepts `co_yield` of anything invocable as `bool()`. `standard/WaitConditions.h` is just a set of functions returning small callables that satisfy that contract. Writing your own wait condition follows the exact same pattern, without touching the core headers. See `docs/coroutines/architecture.md`.

**`CoroutineRunner` can be derived from** to attach extra data to each coroutine it starts -- for example, tagging a coroutine with the game entity it belongs to. `CoroutineHandle` is hashable for this reason, and two protected virtual hooks (`onCoroutineStarted`/`onCoroutineFinished`) keep a subclass's own bookkeeping in sync automatically. See `docs/coroutines/example.md` for a complete example.

**A coroutine's children inherit its extra data automatically.** `onCoroutineStarted(handle, parent)` is told which coroutine was resuming when `handle` was started -- whether via `co_yield childTask()` or a direct `startCoroutine()` call made from inside a coroutine's body -- so a subclass can propagate its own per-coroutine data from parent to child with a couple of lines, with no special handling required at the call site that starts the child.

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Or without CMake: this module has no `.cpp` files to compile, so just add `include/` to your include path.
