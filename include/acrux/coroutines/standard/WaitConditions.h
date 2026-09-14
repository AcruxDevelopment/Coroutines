#pragma once

// ============================================================================
// WaitConditions.hpp — waitForSeconds / waitUntil / waitWhile / waitFor.
//
// This file is NOT part of the coroutine engine. Everything here is built
// entirely on top of the public core API (Coroutine's generic "co_yield
// anything invocable as bool()" support, and CoroutineHandle::isRunning()) --
// Coroutine.h and CoroutineRunner.h have no idea any of this exists.
//
// Each helper below just returns a small callable object with a
// `bool operator()() const`. co_yield-ing that object works because of
// Coroutine::promise_type's generic yield_value<F> overload, not because of
// anything specific to these types. Writing your own wait condition is the
// same one-line pattern: define a small callable, co_yield it (optionally
// through a helper function like the ones below, for a readable call site).
// ============================================================================

#include <chrono>
#include <type_traits>
#include <utility>

#include <acrux/coroutines/CoroutineHandle.h>

namespace acrux::coroutines::standard
{
	namespace detail
	{
		// co_yield waitForSeconds(2.5f) -- resumes once `seconds` of real time
		// has elapsed. The clock starts on this condition's FIRST evaluation
		// (the first time CoroutineRunner::update() checks it after the
		// coroutine suspends), not when waitForSeconds() itself was called.
		class SecondsCondition
		{
		public:
			explicit SecondsCondition(float seconds) noexcept : m_durationSeconds(seconds) {}

			bool operator()() const
			{
				auto now = std::chrono::steady_clock::now();
				if (!m_started)
				{
					m_startTime = now;
					m_started = true;
					return false;
				}
				std::chrono::duration<float> elapsed = now - m_startTime;
				return elapsed.count() >= m_durationSeconds;
			}

		private:
			float m_durationSeconds;
			mutable std::chrono::steady_clock::time_point m_startTime{};
			mutable bool m_started = false;
		};

		// co_yield waitFor(handle) -- resumes once `handle` is no longer
		// running. Implemented purely through CoroutineHandle's public API
		// (isRunning()), so it works the same as co_yield-ing the handle
		// directly (a core feature); this exists only so a call site reading
		// "waitFor" lines up with the other wait* helpers.
		class HandleCondition
		{
		public:
			explicit HandleCondition(CoroutineHandle handle) noexcept : m_handle(handle) {}

			bool operator()() const { return !m_handle.isRunning(); }

		private:
			CoroutineHandle m_handle;
		};

		// co_yield waitWhile(predicate) -- resumes once `predicate()` returns
		// false. A thin wrapper negating whatever predicate was passed in.
		template <typename F>
		class NegatedCondition
		{
		public:
			explicit NegatedCondition(F predicate) : m_predicate(std::move(predicate)) {}

			bool operator()() const { return !m_predicate(); }

		private:
			F m_predicate;
		};
	}

	inline detail::SecondsCondition waitForSeconds(float seconds)
	{
		return detail::SecondsCondition(seconds);
	}

	// Waits for a coroutine that was started earlier (e.g. via the free
	// function startCoroutine() in CoroutineRunner.h) WITHOUT yielding at
	// that time. Equivalent to `co_yield handle` directly — provided here
	// so waiting on a handle reads the same way as the other wait* helpers:
	//
	//     CoroutineHandle bg = startCoroutine(controller.fadeOut(delta)); // fire, don't wait yet
	//     ... do other stuff, possibly across several co_yields ...
	//     co_yield waitFor(bg); // now block until it's done
	inline detail::HandleCondition waitFor(CoroutineHandle handle)
	{
		return detail::HandleCondition(handle);
	}

	// Accepts any callable convertible to bool() — including lambdas that
	// capture `this` or a member coroutine's arguments/locals by value or
	// reference. This is what makes waitUntil/waitWhile usable from
	// non-static member coroutines. Returned by value; co_yield can also
	// accept `predicate` directly without this wrapper, since Coroutine's
	// core yield_value already accepts any bool()-invocable object -- this
	// exists purely so the call site reads "waitUntil".
	template <typename F>
	inline std::decay_t<F> waitUntil(F&& predicate)
	{
		static_assert(std::is_invocable_r_v<bool, std::decay_t<F>&>,
			"waitUntil requires a callable convertible to bool().");
		return std::forward<F>(predicate);
	}

	template <typename F>
	inline detail::NegatedCondition<std::decay_t<F>> waitWhile(F&& predicate)
	{
		static_assert(std::is_invocable_r_v<bool, std::decay_t<F>&>,
			"waitWhile requires a callable convertible to bool().");
		return detail::NegatedCondition<std::decay_t<F>>(std::forward<F>(predicate));
	}
}
