# Coroutine

`#include <acrux/coroutines/Coroutine.h>`

## Description

`Coroutine` is the return type used for any function that suspends itself with `co_yield`. It applies equally to free functions, static functions, and non-static member functions, with or without arguments. `Coroutine` is move-only.

```cpp
Coroutine foo(int a, float b);
Coroutine Enemy::attackSequence(int n);
```

## Members

| Member | Description |
|---|---|
| `m_handle` | The underlying `std::coroutine_handle<promise_type>`. Owned by this object; the destructor destroys it if non-null. |
| `promise_type` | The compiler-required promise type. Member names such as `get_return_object`, `initial_suspend`, and so on are fixed by the coroutine machinery and cannot be renamed. |

### `promise_type`

| Member | Description |
|---|---|
| `initial_suspend()` | Always suspends. Constructing a `Coroutine` builds its frame without running any of its body until it is registered with a `CoroutineRunner`. |
| `final_suspend()` | Always suspends, and is `noexcept`. |
| `unhandled_exception()` | Calls `std::terminate()`. Exceptions thrown inside a coroutine body are not propagated to the caller. |
| `yield_value(std::nullptr_t)` | Handles `co_yield nullptr` — resume on the next frame. |
| `yield_value(CoroutineHandle)` | Handles `co_yield someHandle` — wait for another coroutine, by handle, to finish. |
| `yield_value(Coroutine&&)` | Handles `co_yield otherObj.otherCoroutine(...)`. Starts the child coroutine on the active `CoroutineRunner` and waits for it to finish. Asserts if no `CoroutineRunner` is active. Defined in `CoroutineRunner.h`, since it requires `CoroutineRunner` to be a complete type. |
| `yield_value<F>(F condition)` | Handles `co_yield condition` for any `condition` invocable as `bool()`. Waits until `condition()` returns `true`. This is the extension point `waitForSeconds`/`waitUntil`/`waitWhile`/`waitFor` (see `standard/WaitConditions.h`) are built on; a custom wait condition can be written the same way, without modifying this file. |

## Notes

`Coroutine` is not copyable. A non-static member coroutine captures `this`; if the owning object is destroyed while the coroutine is still suspended, resuming it later is undefined behavior. See the architecture document for how to guard against this.

This header does not include or depend on `standard/WaitConditions.h`. The generic `yield_value<F>` overload above is what allows `waitForSeconds` and similar helpers to exist without this file knowing about them — see the architecture document for the reasoning.
