/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Gzip.
 *
 * Streaming zlib wrappers for gzip inflate and deflate, including multi-buffer compression,
 * checked inflation that distinguishes a valid empty stream from a malformed one, and a
 * stored-block encoder for payloads smaller than 64 KiB.
 *
 */

#ifndef GZIP_H__
#define GZIP_H__

#include <inttypes.h>
#include <iostream>
#include <zlib.h>
#include <vector>

namespace i2p
{
namespace data
{
	class GzipInflator
	{
	public:
		GzipInflator ();
		~GzipInflator ();

		size_t Inflate (const uint8_t * in, size_t inLen, uint8_t * out, size_t outLen);
		// Unlike Inflate(), this distinguishes a valid empty stream from a
		// malformed one and reports the exact uncompressed length.
		bool InflateChecked (const uint8_t * in, size_t inLen, uint8_t * out, size_t outLen, size_t & resultLen);
		/** @note @a os failbit will be set in case of error */
		void Inflate (const uint8_t * in, size_t inLen, std::ostream & os);
		void Inflate (std::istream & in, std::ostream & out);

	private:
		z_stream m_Inflator;
		bool m_IsDirty;
	};

	class GzipDeflator
	{
	public:
		GzipDeflator ();
		~GzipDeflator ();

		void SetCompressionLevel (int level);
		size_t Deflate (const uint8_t * in, size_t inLen, uint8_t * out, size_t outLen);
		size_t Deflate (const std::vector<std::pair<const uint8_t *, size_t>> & bufs, uint8_t * out, size_t outLen);

	private:
		z_stream m_Deflator;
		bool m_IsDirty;
	};

	size_t GzipNoCompression (const uint8_t * in, uint16_t inLen, uint8_t * out, size_t outLen); // for < 64K
	size_t GzipNoCompression (const std::vector<std::pair<const uint8_t *, size_t>> & bufs, uint8_t * out,
		size_t outLen); // for total size < 64K
} // data
} // i2p

#endif /* GZIP_H__ */
