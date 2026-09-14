#pragma once

// ============================================================================
// CoroutineHandle.h — an opaque, cheap-to-copy VALUE TYPE identifying a
// coroutine registered with a CoroutineRunner.
//
// Internally it's still just a packed 32-bit slot index + 32-bit generation
// counter (so CoroutineRunner can find/validate it in O(1), no ABA bugs),
// but it's wrapped in a real type instead of a bare uint64_t so that:
//
//   - you can call handle.isRunning() / handle.stop() directly on it,
//     instead of always having to go through CoroutineRunner or a free
//     function that takes it by reference;
//   - handle.stop() always correctly invalidates the handle it was called
//     on — there's no reference to forget to take, and no way to
//     accidentally stop a copy and leave the original looking "still
//     running";
//   - `if (handle)` reads naturally (explicit operator bool: valid <=> not
//     INVALID_COROUTINE_HANDLE);
//   - it can't accidentally be compared against / mixed up with an
//     unrelated uint64_t (e.g. a frame counter or an entity id).
//
// It's still a small trivially-copyable value (one uint64_t) — pass it
// around, store it, return it by value freely.
// ============================================================================

#include <cstdint>
#include <functional>

namespace acrux::coroutines
{
	class CoroutineRunner; // needs friend access to construct/decode handles

	class CoroutineHandle
	{
	public:
		CoroutineHandle() noexcept = default;

		// Explicit so a handle can't silently decay into an integer/bool in
		// unrelated contexts — only `if (handle)` / `while (!handle)` etc.
		explicit operator bool() const noexcept { return m_id != kInvalidId; }

		friend bool operator==(const CoroutineHandle& a, const CoroutineHandle& b) noexcept { return a.m_id == b.m_id; }
		friend bool operator!=(const CoroutineHandle& a, const CoroutineHandle& b) noexcept { return !(a == b); }

		// Equivalent to CoroutineRunner::isCoroutineRunning(*this).
		// Declared here, defined in CoroutineRunner.h once CoroutineRunner
		// is a complete type.
		bool isRunning() const;

		// Equivalent to CoroutineRunner::stopCoroutine(*this) — stops the
		// coroutine AND resets *this to INVALID_COROUTINE_HANDLE. Declared
		// here, defined in CoroutineRunner.h.
		void stop();

		// The handle's raw packed id (index + generation), exposed so a
		// CoroutineHandle can be used as a key in a hash map/set -- e.g. by a
		// CoroutineRunner subclass tracking its own per-coroutine data (see
		// CoroutineRunner.h). Not meant to be decoded; treat it as opaque.
		uint64_t id() const noexcept { return m_id; }

	private:
		friend class CoroutineRunner;

		static constexpr uint64_t kInvalidId = 0;

		explicit CoroutineHandle(uint64_t id) noexcept : m_id(id) {}

		uint64_t raw() const noexcept { return m_id; }

		uint64_t m_id = kInvalidId;
	};

	inline constexpr CoroutineHandle INVALID_COROUTINE_HANDLE{};
}

// Implementation detail: only CoroutineRunner needs these, and only to
// build/decode the raw id packed inside a CoroutineHandle. Kept out of
// acrux::coroutines so it can't leak into the public API by accident.
namespace detail::acrux::coroutines
{
	constexpr uint32_t IndexBits = 32;

	// index+1 is stored so that (index == 0, generation == 0) never collides
	// with an all-zero (invalid) id.
	inline uint64_t packHandle(uint32_t index, uint32_t generation) noexcept
	{
		return (static_cast<uint64_t>(generation) << IndexBits) |
			   (static_cast<uint64_t>(index) + 1u);
	}

	inline uint32_t unpackIndex(uint64_t raw) noexcept
	{
		return static_cast<uint32_t>(raw & 0xFFFFFFFFull) - 1u;
	}

	inline uint32_t unpackGeneration(uint64_t raw) noexcept
	{
		return static_cast<uint32_t>(raw >> IndexBits);
	}
}

// Lets CoroutineHandle be used directly as a key in std::unordered_map /
// std::unordered_set -- intended for a CoroutineRunner subclass that keeps
// its own side table of per-coroutine data (see CoroutineRunner.h).
template <>
struct std::hash<acrux::coroutines::CoroutineHandle>
{
	size_t operator()(const acrux::coroutines::CoroutineHandle& handle) const noexcept
	{
		return std::hash<uint64_t>{}(handle.id());
	}
};
