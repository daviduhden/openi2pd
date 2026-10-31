/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Modern.
 *
 * Small, portable C++23 vocabulary used by the native tree: enumeration
 * and byte-range concepts, a checked underlying-value conversion, an
 * overload-set helper for std::visit, a move-only scope guard and
 * read-only byte-span views. The header deliberately avoids the
 * expected, format, print and mdspan headers, coroutines and executors,
 * which the supported toolchains do not all provide.
 *
 */

#ifndef I2PD_MODERN_H__
#define I2PD_MODERN_H__

#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace i2p
{
namespace modern
{
	/** An enumeration type, ignoring cv-qualification and references. */
	template <typename T>
	concept Enum = std::is_enum_v<std::remove_cvref_t<T>>;

	/**
	 * A type whose object representation is one byte.
	 *
	 * std::uint8_t and std::int8_t are normally aliases of unsigned char
	 * and signed char, so they are covered without separate alternatives.
	 */
	template <typename T>
	concept ByteLike = std::same_as<std::remove_cvref_t<T>, std::byte> || std::same_as<std::remove_cvref_t<T>, char>
		|| std::same_as<std::remove_cvref_t<T>, signed char> || std::same_as<std::remove_cvref_t<T>, unsigned char>;

	/** A contiguous range whose element type is byte-like. */
	template <typename T>
	concept ContiguousByteRange = std::ranges::contiguous_range<T> && ByteLike<std::ranges::range_value_t<T>>;

	/** A type that converts implicitly to a non-owning string view. */
	template <typename T>
	concept StringViewLike = std::convertible_to<T, std::string_view>;

	/**
	 * The underlying integer value of an enumeration, preserving its type.
	 */
	template <typename T>
		requires Enum<T>
	[[nodiscard]] constexpr std::underlying_type_t<std::remove_cv_t<T>> ToUnderlying (T value) noexcept
	{
		return static_cast<std::underlying_type_t<std::remove_cv_t<T>>> (value);
	}

	/**
	 * Compose several callables into one overload set, typically for
	 * std::visit. Every argument must provide a call operator.
	 */
	template <typename... Ts> struct Overloaded : Ts...
	{
		using Ts::operator()...;
	};
	template <typename... Ts> Overloaded (Ts...) -> Overloaded<Ts...>;

	/**
	 * Move-only scope guard that invokes its callable on destruction
	 * unless dismiss() was called.
	 *
	 * The callable must not throw: the destructor is noexcept and
	 * terminates if the callable escapes with an exception. Use a braced
	 * lambda or a named function object; the deduction guide stores the
	 * callable by value.
	 */
	template <typename F> class ScopeExit
	{
	public:
		explicit ScopeExit (F func) noexcept (std::is_nothrow_move_constructible_v<F>)
			: m_Func (std::move (func))
		{
		}

		ScopeExit (ScopeExit && other) noexcept (std::is_nothrow_move_constructible_v<F>)
			: m_Func (std::move (other.m_Func))
			, m_Active (other.m_Active)
		{
			other.m_Active = false;
		}

		ScopeExit (const ScopeExit &) = delete;
		ScopeExit & operator= (const ScopeExit &) = delete;
		ScopeExit & operator= (ScopeExit &&) = delete;

		~ScopeExit () noexcept
		{
			if (m_Active) m_Func ();
		}

		/** Cancel the pending action. */
		void dismiss () noexcept { m_Active = false; }

	private:
		F m_Func;
		bool m_Active = true;
	};
	template <typename F> ScopeExit (F) -> ScopeExit<F>;

	/** A borrowed, read-only view over raw bytes. */
	using ByteSpan = std::span<const std::byte>;

	/**
	 * Identity overload for an already-typed byte span (including
	 * rvalues).
	 */
	constexpr ByteSpan AsByteSpan (ByteSpan bytes) noexcept
	{
		return bytes;
	}

	/**
	 * Borrow contiguous byte storage as a read-only byte span.
	 *
	 * The range must outlive the returned view; temporaries are rejected
	 * so a dangling view cannot be created accidentally. std::as_bytes
	 * performs the object-representation conversion, so callers do not
	 * need a manual reinterpret_cast and no alignment assumption is
	 * introduced.
	 */
	template <ContiguousByteRange R>
		requires std::ranges::sized_range<R>
	constexpr ByteSpan AsByteSpan (R & range) noexcept
	{
		return std::as_bytes (std::span<const std::ranges::range_value_t<R>> (range));
	}
} // namespace modern
} // namespace i2p

#endif
