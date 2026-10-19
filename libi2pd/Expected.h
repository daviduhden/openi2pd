/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef I2PD_EXPECTED_H__
#define I2PD_EXPECTED_H__

#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

/**
 * Small, portable subset of i2p::expected used by the fork.
 *
 * Some supported toolchains -- notably the OpenBSD base libc++ used by the
 * CI image -- advertise C++23 but do not ship <expected>. Keeping this shim
 * lets the native code remain C++23 (concepts, ranges, etc.) while preserving
 * a value-or-error API and the same syntax at call sites. The implementation
 * intentionally exposes only the operations used by this tree.
 */
namespace i2p
{
template <class E> class unexpected
{
public:
	explicit unexpected (E error)
		: m_Error (std::move (error))
	{
	}

	const E & error () const & noexcept { return m_Error; }
	E & error () & noexcept { return m_Error; }
	E && error () && noexcept { return std::move (m_Error); }

private:
	E m_Error;
};

template <class E> unexpected (E) -> unexpected<E>;

template <class T, class E> class expected
{
public:
	expected (T value)
		: m_Value (std::in_place_index<0>, std::move (value))
	{
	}

	template <class E2>
	expected (unexpected<E2> && error)
		: m_Value (std::in_place_index<1>, E (std::move (error).error ()))
	{
	}

	bool has_value () const noexcept { return m_Value.index () == 0; }
	explicit operator bool () const noexcept { return has_value (); }

	T & operator* () & { return std::get<0> (m_Value); }
	const T & operator* () const & { return std::get<0> (m_Value); }
	T && operator* () && { return std::get<0> (std::move (m_Value)); }

	T * operator->() { return std::addressof (std::get<0> (m_Value)); }
	const T * operator->() const { return std::addressof (std::get<0> (m_Value)); }

	T & value () &
	{
		if (!has_value ()) throw std::logic_error ("expected has no value");
		return std::get<0> (m_Value);
	}
	const T & value () const &
	{
		if (!has_value ()) throw std::logic_error ("expected has no value");
		return std::get<0> (m_Value);
	}
	T && value () &&
	{
		if (!has_value ()) throw std::logic_error ("expected has no value");
		return std::get<0> (std::move (m_Value));
	}

	E & error () &
	{
		if (has_value ()) throw std::logic_error ("expected has a value");
		return std::get<1> (m_Value);
	}
	const E & error () const &
	{
		if (has_value ()) throw std::logic_error ("expected has a value");
		return std::get<1> (m_Value);
	}

private:
	std::variant<T, E> m_Value;
};

template <class E> class expected<void, E>
{
public:
	expected () noexcept = default;

	template <class E2>
	expected (unexpected<E2> && error)
		: m_Error (std::in_place, E (std::move (error).error ()))
	{
	}

	bool has_value () const noexcept { return !m_Error.has_value (); }
	explicit operator bool () const noexcept { return has_value (); }
	void value () const
	{
		if (!has_value ()) throw std::logic_error ("expected has no value");
	}

	E & error () &
	{
		if (has_value ()) throw std::logic_error ("expected has a value");
		return *m_Error;
	}
	const E & error () const &
	{
		if (has_value ()) throw std::logic_error ("expected has a value");
		return *m_Error;
	}

private:
	std::optional<E> m_Error;
};
}

#endif
