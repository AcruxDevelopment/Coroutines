#pragma once

// ============================================================================
// Coroutine.hpp — the coroutine return type (the "Task") and its
// promise_type.
//
// Nothing here needs to change for arguments or member coroutines: in C++20
// the promise_type used for a coroutine is selected purely from its RETURN
// TYPE (Coroutine), never from whether the function is free, static, or a
// non-static member, and never from its parameter list. So:
//
//     Coroutine foo(int a, float b);           // args: just works
//     Coroutine Enemy::attackSequence(int n);   // member: just works
//
// The one thing this library genuinely cannot do for you: a non-static
// member coroutine captures `this` in its coroutine frame like any other
// closure. If the owning object is destroyed while the coroutine is still
// suspended, resuming it is a use-after-free. Stop a member coroutine
// (CoroutineRunner::stopCoroutine) in the owner's destructor if there's any
// chance it's still running.
//
// What a coroutine can co_yield is intentionally small and closed here:
//   - nullptr            -- wait one frame
//   - a CoroutineHandle   -- wait for another coroutine to finish
//   - a child Coroutine   -- start it and wait for it to finish (defined in
//                            CoroutineRunner.hpp; needs CoroutineRunner)
//   - anything invocable as bool() -- wait until it returns true
//
// That last case is deliberately generic rather than a fixed enum of "kinds
// of waiting." waitForSeconds()/waitUntil()/waitWhile()/waitFor() (see
// coroutines/standard/WaitConditions.h) are just ordinary functions that
// return small callable objects satisfying this same bool() contract --
// this header has no idea they exist, and doesn't need to. Anyone is free
// to write their own wait conditions the same way, without touching this
// file or CoroutineRunner.hpp.
// ============================================================================

#include <coroutine>
#include <type_traits>
#include <utility>

#include <acrux/coroutines/CoroutineHandle.h>
#include <acrux/coroutines/Predicate.h>

namespace acrux::coroutines
{
	struct Coroutine
	{
		struct promise_type
		{
			// Note: get_return_object, initial_suspend, final_suspend,
			// unhandled_exception, return_void, and yield_value are compiler
			// hook names mandated by the coroutine machinery and cannot be
			// renamed to match the library's camelCase convention.

			enum class WaitKind : uint8_t
			{
				NextFrame,
				Condition,
				NestedHandle
			};

			WaitKind m_waitKind = WaitKind::NextFrame;
			CoroutineHandle m_nestedHandle = INVALID_COROUTINE_HANDLE;
			Predicate m_condition;
			CoroutineHandle m_assignedId = INVALID_COROUTINE_HANDLE;

			Coroutine get_return_object()
			{
				return Coroutine{ std::coroutine_handle<promise_type>::from_promise(*this) };
			}

			std::suspend_always initial_suspend() noexcept { return {}; }
			std::suspend_always final_suspend() noexcept { return {}; }
			void unhandled_exception() { std::terminate(); }
			void return_void() noexcept {}

			// co_yield nullptr -- wait exactly one frame.
			std::suspend_always yield_value(std::nullptr_t) noexcept
			{
				m_waitKind = WaitKind::NextFrame;
				return {};
			}

			// co_yield someHandle -- wait for another coroutine to finish.
			std::suspend_always yield_value(CoroutineHandle handle) noexcept
			{
				m_waitKind = WaitKind::NestedHandle;
				m_nestedHandle = handle;
				return {};
			}

			// co_yield myObject.myFunc(...) -- defined in CoroutineRunner.hpp,
			// see the file-level comment above for why.
			std::suspend_always yield_value(Coroutine&& childTask);

			// co_yield <anything invocable as bool()> -- wait until it
			// returns true. This is the extension point every wait-condition
			// helper (waitForSeconds, waitUntil, waitFor, ...) is built on;
			// see coroutines/standard/WaitConditions.h for the reference
			// implementations, or write your own the same way.
			template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<bool, const F&>>>
			std::suspend_always yield_value(F condition)
			{
				m_waitKind = WaitKind::Condition;
				m_condition = Predicate(std::move(condition));
				return {};
			}
		};

		std::coroutine_handle<promise_type> m_handle;

		Coroutine() noexcept : m_handle(nullptr) {}
		explicit Coroutine(std::coroutine_handle<promise_type> h) noexcept : m_handle(h) {}
		~Coroutine() { if (m_handle) m_handle.destroy(); }

		Coroutine(const Coroutine&) = delete;
		Coroutine& operator=(const Coroutine&) = delete;

		Coroutine(Coroutine&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }
		Coroutine& operator=(Coroutine&& other) noexcept
		{
			if (this != &other)
			{
				if (m_handle) m_handle.destroy();
				m_handle = other.m_handle;
				other.m_handle = nullptr;
			}
			return *this;
		}
	};
}
