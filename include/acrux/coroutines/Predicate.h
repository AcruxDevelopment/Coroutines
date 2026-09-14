#pragma once

// ============================================================================
// Predicate.hpp — small-buffer-optimized type-erased bool() callable.
//
// Stores any callable (lambda, functor) up to kBufferSize bytes INLINE — no
// heap allocation — which is exactly what you need for the common case of a
// lambda capturing `this` and/or a couple of coroutine-local values, e.g.:
//
//     WaitUntil([this]{ return this->targetInRange; })
//     WaitUntil([hp = enemy->hp]{ return hp() <= 0; })
// ============================================================================

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <cassert>

namespace acrux::coroutines
{
	class Predicate
	{
	public:
		Predicate() noexcept = default;

		template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Predicate>>>
		Predicate(F&& f)
		{
			using Fn = std::decay_t<F>;
			static_assert(sizeof(Fn) <= BufferSize,
				"Predicate: captured callable is too large for the inline buffer. "
				"Capture by pointer/reference (e.g. [this]) instead of by value, "
				"or increase Predicate::kBufferSize.");
			static_assert(alignof(Fn) <= alignof(std::max_align_t),
				"Predicate: captured callable's alignment exceeds max_align_t.");

			::new (static_cast<void*>(&m_storage)) Fn(std::forward<F>(f));
			m_vtable = &VTableFor<Fn>::table;
		}

		Predicate(const Predicate&) = delete;
		Predicate& operator=(const Predicate&) = delete;

		Predicate(Predicate&& other) noexcept { moveFrom(other); }

		Predicate& operator=(Predicate&& other) noexcept
		{
			if (this != &other)
			{
				destroy();
				moveFrom(other);
			}
			return *this;
		}

		~Predicate() { destroy(); }

		bool operator()() const
		{
			assert(m_vtable && "Predicate invoked while empty");
			return m_vtable->invoke(&m_storage);
		}

		explicit operator bool() const noexcept { return m_vtable != nullptr; }

	private:
		static constexpr size_t BufferSize = 32;

		struct VTable
		{
			bool (*invoke)(const void*);
			void (*move)(void* dst, void* src);
			void (*destroy)(void*);
		};

		template <typename Fn>
		struct VTableFor
		{
			static bool invoke(const void* p)
			{
				return (*reinterpret_cast<const Fn*>(p))();
			}
			static void move(void* dst, void* src)
			{
				::new (dst) Fn(std::move(*reinterpret_cast<Fn*>(src)));
				reinterpret_cast<Fn*>(src)->~Fn();
			}
			static void destroy(void* p)
			{
				reinterpret_cast<Fn*>(p)->~Fn();
			}
			static inline const VTable table{ &invoke, &move, &destroy };
		};

		void moveFrom(Predicate& other) noexcept
		{
			m_vtable = other.m_vtable;
			if (m_vtable)
			{
				m_vtable->move(&m_storage, &other.m_storage);
				other.m_vtable = nullptr;
			}
		}
		void destroy() noexcept
		{
			if (m_vtable)
			{
				m_vtable->destroy(&m_storage);
				m_vtable = nullptr;
			}
		}

		alignas(std::max_align_t) unsigned char m_storage[BufferSize];
		const VTable* m_vtable = nullptr;
	};
}
