#pragma once

// ============================================================================
// CoroutineRunner.hpp — the engine: a generational slot map of running
// coroutines.
//
//   - CoroutineHandle packs a 32-bit slot index + a 32-bit generation.
//   - startCoroutine / stopCoroutine / isCoroutineRunning are O(1) in the
//     common case (index straight into a vector, generation check to reject
//     stale/reused handles — no ABA bugs).
//   - Freed slots go on a free list and get reused before growing.
//   - update() reuses a persistent scratch buffer for finished slots instead
//     of allocating one every frame.
//   - getActiveCount() is tracked incrementally (O(1)) instead of
//     re-counting.
//
// Growing the backing store (i.e. adding a coroutine when there is no free
// slot to reuse) has to be deferred to the start of the next update(),
// because update() holds a reference into the slot array while it resumes
// coroutines, and those coroutines are allowed to start new ones. A
// std::vector resize while that reference is alive would dangle it. Reusing
// an already-existing free slot has no such restriction (no reallocation),
// so that path is handled immediately instead of staged.
//
// EXTENDING THIS: CoroutineRunner is not `final` specifically so it can be
// derived from. A subclass can add its own overload(s) of startCoroutine
// that accept extra data -- an EntityHandle in a game engine, or whatever
// your program needs -- and track that data itself, keyed by CoroutineHandle
// (which is hashable; see CoroutineHandle::id()/std::hash specialization in
// CoroutineHandle.h). onCoroutineStarted(handle, parent)/onCoroutineFinished(handle)
// are called at the right points to keep that side table in sync
// automatically -- `parent` lets a subclass propagate its data from a
// coroutine to any child it starts (via `co_yield childTask()` or a direct
// startCoroutine() call from within its body). This base class has no
// opinion on what "extra data" means -- that's entirely up to the derived
// class. See coroutines/example.md for a worked example.
// ============================================================================

#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>
#include <cassert>

#include <acrux/coroutines/CoroutineHandle.h>
#include <acrux/coroutines/Coroutine.h>

namespace acrux::coroutines
{
	class CoroutineRunner
	{
	private:
		struct Slot
		{
			Coroutine m_task;
			uint32_t m_generation = 0;
			bool m_alive = false;
		};

		std::vector<Slot> m_slots;
		std::vector<uint32_t> m_freeIndices;

		struct PendingTask
		{
			uint32_t m_index;
			Coroutine m_task;
			CoroutineHandle m_parent; // whichever coroutine was resuming when this was started, or invalid
		};

		// Tasks that need a brand-new slot (no free slot to reuse) get staged
		// here and only actually inserted into `m_slots` at the top of update(),
		// because that's the only point at which growing `m_slots` is safe.
		std::vector<PendingTask> m_pendingTasks;
		uint32_t m_logicalSize = 0; // m_slots.size() + slots reserved for pending growth

		std::vector<uint32_t> m_deadIndices; // scratch buffer, reused every frame
		size_t m_liveCount = 0;

		// Set for the duration of a single h.resume() call in update() (see
		// step 2 below), otherwise INVALID_COROUTINE_HANDLE. Lets
		// startCoroutine() record which coroutine, if any, was actually
		// running when it was called -- whether via `co_yield childTask()`
		// or a direct startCoroutine() call made from inside a coroutine's
		// body -- so onCoroutineStarted() can tell a subclass "this one was
		// started by that one" and propagate whatever it needs to.
		CoroutineHandle m_currentlyResuming = INVALID_COROUTINE_HANDLE;

		bool shouldResume(Coroutine::promise_type& promise) const
		{
			switch (promise.m_waitKind)
			{
				case Coroutine::promise_type::WaitKind::NextFrame:
					return true;
				case Coroutine::promise_type::WaitKind::Condition:
					return promise.m_condition();
				case Coroutine::promise_type::WaitKind::NestedHandle:
					return !isCoroutineRunning(promise.m_nestedHandle);
			}
			return true;
		}

	protected:
		// Called once a coroutine becomes live in the slot map -- either
		// immediately (startCoroutine() reusing a free slot) or at the start
		// of the next update() (a newly-grown slot). Not called for a task
		// that never becomes live (e.g. stopped again before its first
		// update()).
		//
		// `parent` is whichever coroutine was actually resuming at the
		// moment `handle` was started -- set whether it was started via
		// `co_yield childTask()` or a plain `startCoroutine()` call made
		// from inside a coroutine's body -- or INVALID_COROUTINE_HANDLE if
		// it was started from outside any coroutine (e.g. from main()).
		// A subclass can use this to propagate its own per-coroutine data
		// from parent to child automatically; see coroutines/example.md.
		//
		// Override to hook "a coroutine started" in a subclass; the default
		// implementation does nothing.
		virtual void onCoroutineStarted(CoroutineHandle handle, CoroutineHandle parent) { (void)handle; (void)parent; }

		// Called when a coroutine's slot is reclaimed, whether it finished
		// naturally (in update()) or was stopped explicitly
		// (stopCoroutine()). Only called for a handle that previously
		// triggered onCoroutineStarted(). Override to clean up any
		// per-coroutine data a subclass is tracking; the default
		// implementation does nothing.
		virtual void onCoroutineFinished(CoroutineHandle handle) { (void)handle; }

	public:
		static CoroutineRunner* m_instance;

		CoroutineRunner() { m_instance = this; }
		virtual ~CoroutineRunner() { if (m_instance == this) m_instance = nullptr; }

		CoroutineRunner(const CoroutineRunner&) = delete;
		CoroutineRunner& operator=(const CoroutineRunner&) = delete;

		// O(1): reuses a free slot in place if one exists (no reallocation, safe
		// even while update() is mid-iteration). Only falls back to the staging
		// queue when the backing store actually needs to grow.
		CoroutineHandle startCoroutine(Coroutine task)
		{
			if (!task.m_handle) return INVALID_COROUTINE_HANDLE;

			if (!m_freeIndices.empty())
			{
				uint32_t index = m_freeIndices.back();
				m_freeIndices.pop_back();

				CoroutineHandle newHandle(detail::acrux::coroutines::packHandle(index, m_slots[index].m_generation));
				task.m_handle.promise().m_assignedId = newHandle;

				m_slots[index].m_task = std::move(task);
				m_slots[index].m_alive = true;
				++m_liveCount;
				onCoroutineStarted(newHandle, m_currentlyResuming);
				return newHandle;
			}

			uint32_t index = m_logicalSize++;
			CoroutineHandle newHandle(detail::acrux::coroutines::packHandle(index, 0));
			task.m_handle.promise().m_assignedId = newHandle;
			m_pendingTasks.push_back(PendingTask{ index, std::move(task), m_currentlyResuming });
			return newHandle;
		}

		void startSingleCoroutine(CoroutineHandle& slotHandle, Coroutine task)
		{
			stopCoroutine(slotHandle);
			slotHandle = startCoroutine(std::move(task));
		}

		// O(1): decode the slot index directly; generation check rejects stale
		// handles from a slot that has since been reused (no ABA bugs). Falls
		// back to a short linear scan only for handles started earlier this
		// same frame and not yet drained into `m_slots`.
		bool isCoroutineRunning(CoroutineHandle handle) const
		{
			if (handle == INVALID_COROUTINE_HANDLE) return false;

			uint32_t index = detail::acrux::coroutines::unpackIndex(handle.raw());
			uint32_t generation = detail::acrux::coroutines::unpackGeneration(handle.raw());

			if (index < m_slots.size() && m_slots[index].m_alive && m_slots[index].m_generation == generation)
			{
				return true;
			}
			for (const auto& pending : m_pendingTasks)
			{
				if (pending.m_index == index && pending.m_task.m_handle && pending.m_task.m_handle.promise().m_assignedId == handle)
				{
					return true;
				}
			}
			return false;
		}

		// O(1) in the common case; same fallback as isCoroutineRunning for
		// handles stopped in the same frame they were started.
		void stopCoroutine(CoroutineHandle& handle)
		{
			if (handle == INVALID_COROUTINE_HANDLE) return;

			uint32_t index = detail::acrux::coroutines::unpackIndex(handle.raw());
			uint32_t generation = detail::acrux::coroutines::unpackGeneration(handle.raw());

			if (index < m_slots.size() && m_slots[index].m_alive && m_slots[index].m_generation == generation)
			{
				m_slots[index].m_task = Coroutine{};
				m_slots[index].m_alive = false;
				++m_slots[index].m_generation;
				m_freeIndices.push_back(index);
				--m_liveCount;
				onCoroutineFinished(handle);
				handle = INVALID_COROUTINE_HANDLE;
				return;
			}

			for (auto it = m_pendingTasks.begin(); it != m_pendingTasks.end(); ++it)
			{
				if (it->m_index == index && it->m_task.m_handle && it->m_task.m_handle.promise().m_assignedId == handle)
				{
					m_pendingTasks.erase(it);
					break;
				}
			}
			handle = INVALID_COROUTINE_HANDLE;
		}

		void update()
		{
			// Step 1: grow the backing store exactly once (if needed) and drain
			// staged insertions into it. This is the only place `m_slots`
			// grows, so it's safe to do it here, before any references into it
			// are taken below.
			if (!m_pendingTasks.empty())
			{
				if (m_logicalSize > m_slots.size())
				{
					m_slots.resize(m_logicalSize);
				}
				for (auto& pending : m_pendingTasks)
				{
					CoroutineHandle handle = pending.m_task.m_handle.promise().m_assignedId;
					m_slots[pending.m_index].m_task = std::move(pending.m_task);
					m_slots[pending.m_index].m_alive = true;
					++m_liveCount;
					onCoroutineStarted(handle, pending.m_parent);
				}
				m_pendingTasks.clear();
			}

			// Step 2: tick every live slot. New coroutines started from within
			// a resumed coroutine go through startCoroutine, which either lands
			// in an existing free slot immediately or gets staged for next
			// frame — either way `m_slots` itself never reallocates during this
			// loop. m_currentlyResuming is set for the duration of each
			// h.resume() call so that a nested startCoroutine() (from
			// `co_yield childTask()` or a direct call in the coroutine's body)
			// can see who's resuming it right now.
			m_deadIndices.clear();

			const size_t size = m_slots.size();
			for (size_t i = 0; i < size; ++i)
			{
				Slot& slot = m_slots[i];
				if (!slot.m_alive) continue;

				auto h = slot.m_task.m_handle;
				if (!h || h.done())
				{
					m_deadIndices.push_back(static_cast<uint32_t>(i));
					continue;
				}

				if (shouldResume(h.promise()))
				{
					m_currentlyResuming = h.promise().m_assignedId;
					h.resume();
					m_currentlyResuming = INVALID_COROUTINE_HANDLE;
				}

				if (!h || h.done())
				{
					m_deadIndices.push_back(static_cast<uint32_t>(i));
				}
			}

			// Step 3: reclaim finished slots.
			for (uint32_t index : m_deadIndices)
			{
				Slot& slot = m_slots[index];
				if (!slot.m_alive) continue; // may have been explicitly stopped already this frame
				// An alive slot always has a valid coroutine handle -- startCoroutine()
				// rejects an empty Coroutine before a slot is ever marked alive.
				CoroutineHandle handle = slot.m_task.m_handle.promise().m_assignedId;
				slot.m_task = Coroutine{};
				slot.m_alive = false;
				++slot.m_generation;
				m_freeIndices.push_back(index);
				--m_liveCount;
				onCoroutineFinished(handle);
			}
		}

		size_t getActiveCount() const { return m_liveCount + m_pendingTasks.size(); }
	};

	inline CoroutineRunner* CoroutineRunner::m_instance = nullptr;

	// ----------------------------------------------------------------------------
	// Out-of-line definition of promise_type::yield_value(Coroutine&&). Needs
	// CoroutineRunner to be a complete type, so it lives here rather than inline
	// inside Coroutine.hpp (see the comment at the top of that file).
	// ----------------------------------------------------------------------------
	inline std::suspend_always Coroutine::promise_type::yield_value(Coroutine&& childTask)
	{
		assert(CoroutineRunner::m_instance &&
			   "co_yield of a child Coroutine requires an active CoroutineRunner "
			   "(construct one before starting/resuming coroutines).");

		CoroutineHandle childHandle = CoroutineRunner::m_instance->startCoroutine(std::move(childTask));

		m_waitKind = WaitKind::NestedHandle;
		m_nestedHandle = childHandle;
		return {};
	}

	// Free-function convenience wrapper around CoroutineRunner::startCoroutine,
	// using the active runner (CoroutineRunner::m_instance) — the same
	// singleton that co_yield of a child Coroutine relies on internally.
	//
	// Starts `task` immediately and returns its handle WITHOUT suspending the
	// caller, so it can be called from anywhere: main(), a plain function, or
	// from inside another running coroutine. This is the "fire it, keep the
	// handle, wait later (or never)" entry point:
	//
	//     CoroutineHandle bg = startCoroutine(controller.fadeOut(delta));
	//     // ... keep going, do other work ...
	//     co_yield waitFor(bg); // wait on it whenever you're ready (optional)
	//
	// (Contrast with `co_yield controller.fadeOut(delta);`, which starts AND
	// immediately blocks the caller until it finishes.)
	//
	// This always calls CoroutineRunner::startCoroutine specifically -- if the
	// active runner is actually a subclass with its own extended
	// startCoroutine() overload(s), call those directly on the runner instance
	// instead of through this free function.
	inline CoroutineHandle startCoroutine(Coroutine task)
	{
		assert(CoroutineRunner::m_instance &&
			   "startCoroutine() requires an active CoroutineRunner "
			   "(construct one before starting/resuming coroutines).");

		return CoroutineRunner::m_instance->startCoroutine(std::move(task));
	}

	// Free-function convenience wrapper around CoroutineRunner::stopCoroutine,
	// using the active runner (CoroutineRunner::m_instance).
	//
	// Takes `handle` by REFERENCE, same as the member function — it stops the
	// coroutine AND resets `handle` to INVALID_COROUTINE_HANDLE, so a stale
	// handle can never accidentally be reused or re-stopped:
	//
	//     CoroutineHandle bg = startCoroutine(controller.fadeOut(delta));
	//     ...
	//     stopCoroutine(bg); // bg is now INVALID_COROUTINE_HANDLE
	//     stopCoroutine(bg); // safe no-op, doesn't crash or double-free
	inline void stopCoroutine(CoroutineHandle& handle)
	{
		handle.stop();
	}

	// ----------------------------------------------------------------------------
	// Out-of-line definitions of CoroutineHandle::isRunning()/stop(). Needs
	// CoroutineRunner to be a complete type, so they live here rather than
	// inline inside CoroutineHandle.h (see the comment at the top of that
	// file). This is what lets you write `buster.stop();` /
	// `buster.isRunning()` directly on a handle instead of going through
	// CoroutineRunner or a free function.
	// ----------------------------------------------------------------------------
	inline bool CoroutineHandle::isRunning() const
	{
		assert(CoroutineRunner::m_instance &&
			   "CoroutineHandle::isRunning() requires an active CoroutineRunner "
			   "(construct one before starting/resuming coroutines).");

		return CoroutineRunner::m_instance->isCoroutineRunning(*this);
	}

	inline void CoroutineHandle::stop()
	{
		assert(CoroutineRunner::m_instance &&
			   "CoroutineHandle::stop() requires an active CoroutineRunner "
			   "(construct one before starting/resuming coroutines).");

		CoroutineRunner::m_instance->stopCoroutine(*this);
	}
}
