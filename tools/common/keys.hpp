/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: keys.
 *
 * One authority for loading and storing I2P destination key files. Loading
 * requires the buffer to be consumed exactly, storing uses the core durable
 * atomic private writer, and a secret buffer wipes itself on every exit path.
 * The on-disk byte layout is unchanged.
 */

#ifndef I2PD_TOOLS_COMMON_KEYS_HPP
#define I2PD_TOOLS_COMMON_KEYS_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "FS.h"
#include "Identity.h"
#include "file.hpp"

namespace i2p::tools
{
/**
	 * Owner of a private-key serialization. The destructor wipes the bytes and
	 * the type is non-copyable/non-movable so it cannot be accidentally left
	 * behind in a second buffer.
	 *
	 * The wipe covers the current `size()` bytes only: growing the buffer
	 * leaves the freed, smaller allocation uncleansed, and `resize(0)` wipes
	 * nothing. Treat this as a scoped staging buffer for one serialization, not
	 * as a long-lived secret store, and do not shrink it after it held key
	 * material.
	 */
class SecureBuffer
{
public:
	SecureBuffer () = default;
	explicit SecureBuffer (size_t size)
		: m_Data (size)
	{
	}
	~SecureBuffer () { Wipe (m_Data); }
	SecureBuffer (const SecureBuffer &) = delete;
	SecureBuffer & operator= (const SecureBuffer &) = delete;
	SecureBuffer (SecureBuffer &&) = delete;
	SecureBuffer & operator= (SecureBuffer &&) = delete;

	uint8_t * data () { return m_Data.data (); }
	const uint8_t * data () const { return m_Data.data (); }
	size_t size () const { return m_Data.size (); }
	bool empty () const { return m_Data.empty (); }
	void resize (size_t size) { m_Data.resize (size); }
	std::span<uint8_t> span () { return m_Data; }
	std::span<const uint8_t> span () const { return m_Data; }

private:
	std::vector<uint8_t> m_Data;
};

/**
	 * Read a destination key file and require the parser to consume every byte.
	 * A trailing block is rejected instead of silently ignored.
	 *
	 * The read is capped by the `ReadFile` default (16 MiB), so an oversized
	 * file is refused before a buffer is allocated for it. The whole input is
	 * wiped on both paths, including a parse failure.
	 *
	 * @param keys Receives the parsed destination. `PrivateKeys::FromBuffer`
	 *             resets itself before parsing, so on failure this object is
	 *             left empty rather than partially populated; callers must
	 *             still stop and must not sign with it.
	 * @return Success, or the file error, or `bad keys file format` for a
	 *         truncated file or one with trailing bytes.
	 */
inline i2p::expected<void, std::string> LoadPrivateKeys (const std::string & path, i2p::data::PrivateKeys & keys)
{
	auto input = ReadFile (path);
	if (!input) return i2p::unexpected (input.error ());
	const size_t size = input->size ();
	const size_t consumed = keys.FromBuffer (input->data (), size);
	Wipe (*input);
	if (!consumed || consumed != size) return i2p::unexpected ("bad keys file format");
	return { };
}

/**
	 * Replace a key file through the core staged writer: same directory,
	 * fsync, owner-only mode and no in-place truncation. The original bytes are
	 * unchanged, so existing key files stay interoperable.
	 *
	 * The byte layout is untouched, but the write is not a no-op when @a data
	 * is empty: an empty span replaces the file with a zero-length file, so a
	 * failed or skipped serialization must be checked by the caller before this
	 * is called. A `false` result after the POSIX rename means the new file may
	 * already be visible while parent-directory durability could not be
	 * confirmed; report it as a failure and let the operator inspect the file
	 * rather than retrying with different bytes.
	 *
	 * @warning The parent hierarchy must be trusted and owned by the caller; the
	 *          writer relies on it against concurrent symlink or reparse
	 *          changes.
	 */
inline bool SavePrivateKeys (const std::string & path, std::span<const uint8_t> data)
{
	if (path.empty ()) return false;
	const std::string_view contents = data.empty ()
		? std::string_view ()
		: std::string_view (reinterpret_cast<const char *> (data.data ()), data.size ());
	return i2p::fs::AtomicWritePrivate (path, contents);
}

/**
	 * True when two paths name the same file, either textually after
	 * canonicalization or through the same device/inode. Used to refuse an
	 * output that would destroy its own input.
	 *
	 * This is a convenience guard for an obvious operator mistake, not a
	 * security boundary: the comparison happens before the write, so a rename
	 * between the check and the atomic replace is not detected, and a missing
	 * path falls back to a textual comparison. Callers must still write through
	 * key-material-safe writers rather than relying on this result.
	 */
inline bool SameFile (const std::string & first, const std::string & second)
{
	if (first.empty () || second.empty ()) return false;
	std::error_code error;
	if (std::filesystem::equivalent (first, second, error) && !error) return true;
	error.clear ();
	const auto a = std::filesystem::weakly_canonical (first, error);
	if (error) return first == second;
	error.clear ();
	const auto b = std::filesystem::weakly_canonical (second, error);
	if (error) return first == second;
	return a == b;
}
} // namespace i2p::tools

#endif // I2PD_TOOLS_COMMON_KEYS_HPP
