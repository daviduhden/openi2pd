/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RouterInfo.
 *
 * Implements RouterInfo and LocalRouterInfo wire parsing, validation, signing,
 * capability/transport extraction and persistence, including per-family
 * NTCP2/SSU2 addresses, introducers, options and congestion metadata. Address
 * hosts, ports, MTU, versions, introducers and signatures from peers are
 * strictly bounded, while local updates rebuild the signed buffer and save it
 * asynchronously.
 *
 */

#include <stdio.h>
#include <string.h>
#include "I2PEndian.h"
#include <fstream>
#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <charconv>
#include <boost/algorithm/string.hpp> // for boost::to_lower
#include "version.h"
#include "util.h"
#include "Crypto.h"
#include "Base.h"
#include "Timestamp.h"
#include "Log.h"
#include "Transports.h"
#include "NetDb.hpp"
#include "RouterContext.h"
#include "CryptoKey.h"
#if OPENSSL_PQ
#include "PostQuantum.h"
#endif
#include "RouterInfo.h"

namespace i2p
{
namespace data
{
	namespace
	{
		// Copy an address into a distinct object restricted to one address
		// family. A non-published SSU2 or NTCP2 address can declare both "4"
		// and "6" in its caps, but host, published state, introducers, MTU and
		// version are per-family state. Java publishes separate v4 and v6
		// RouterAddresses for this reason; keep the same independence here so a
		// v4 firewall/port update cannot modify the v6 address (and vice
		// versa).
		void CopyAddressForFamily (const RouterInfo::Address & src, RouterInfo::Address & dst, bool v4)
		{
			dst.transportStyle = src.transportStyle;
			dst.host = src.host;
			dst.s = src.s;
			dst.i = src.i;
			dst.v = src.v;
			dst.pqVersions = src.pqVersions;
			dst.port = src.port;
			dst.date = src.date;
			dst.published = src.published;
			dst.caps = static_cast<uint8_t> (
				(src.caps & ~(RouterInfo::AddressCaps::eV4 | RouterInfo::AddressCaps::eV6))
				| (v4 ? RouterInfo::AddressCaps::eV4 : RouterInfo::AddressCaps::eV6));
			if (src.ssu)
				dst.ssu = std::make_unique<RouterInfo::SSUExt> (*src.ssu);
			else
				dst.ssu.reset ();
		}

		// "router.version" is a dotted version string. i2pd stores it as a
		// packed integer using the same MAKE_VERSION_NUMBER() scheme as the
		// NetDb thresholds. Concatenating every digit (the previous behavior)
		// misparsed multi-digit components: "0.10.0" became 100 instead of
		// 1000, which is below every 0.9.6x threshold.
		int ParseRouterVersion (std::string_view value)
		{
			int parts[3] = { 0, 0, 0 };
			size_t part = 0;
			int current = 0;
			for (char ch : value)
			{
				if (ch >= '0' && ch <= '9')
					current = std::min (current * 10 + (ch - '0'), 9999);
				else if (ch == '.')
				{
					if (part >= 2) break; // ignore components after micro
					parts[part++] = current;
					current = 0;
				}
				else
					break; // qualifier such as "-rc1"
			}
			if (part < 3) parts[part] = current;
			// Minor and micro are two decimal digits in MAKE_VERSION_NUMBER().
			int minor = std::min (parts[1], 99);
			int micro = std::min (parts[2], 99);
			return (parts[0] * 100 + minor) * 100 + micro;
		}

		int BandwidthRank (char cap)
		{
			switch (cap)
			{
			case 'K': return 1;
			case 'L': return 2;
			case 'M': return 3;
			case 'N': return 4;
			case 'O': return 5;
			case 'P': return 6;
			case 'X': return 7;
			default: return 0;
			}
		}

		char BandwidthCapForRank (int rank)
		{
			static const char caps[] = { 0, 'K', 'L', 'M', 'N', 'O', 'P', 'X' };
			return (rank >= 1 && rank <= 7) ? caps[rank] : 0;
		}

		// Derive supported/reachable/published transports from address objects.
		// Both the RouterInfo parser and UpdateSupportedTransports() use this,
		// so a parsed RouterInfo and the same object after a local address
		// update agree.
		void ComputeAddressTransports (const RouterInfo::Addresses & addresses,
			RouterInfo::CompatibleTransports & supported, RouterInfo::CompatibleTransports & reachable,
			RouterInfo::CompatibleTransports & published)
		{
			supported = 0;
			reachable = 0;
			published = 0;
			for (const auto & address : addresses)
			{
				if (!address) continue;
				RouterInfo::CompatibleTransports transports = 0;
				switch (address->transportStyle)
				{
				case RouterInfo::eTransportNTCP2:
					if (address->IsV4 ()) transports |= RouterInfo::eNTCP2V4;
					if (address->IsV6 ())
						transports |= (i2p::util::net::IsYggdrasilAddress (address->host) ? RouterInfo::eNTCP2V6Mesh
																						  : RouterInfo::eNTCP2V6);
					// An outbound-only NTCP2 address has no host/port/i and must
					// not make the router look reachable or published.
					if (address->IsPublishedNTCP2 () && address->port && !address->host.is_unspecified ())
					{
						published |= transports;
						reachable |= transports;
					}
					break;
				case RouterInfo::eTransportSSU2:
					if (address->IsV4 ()) transports |= RouterInfo::eSSU2V4;
					if (address->IsV6 ()) transports |= RouterInfo::eSSU2V6;
					if (address->published && address->port && !address->host.is_unspecified ())
						published |= transports;
					if (address->IsReachableSSU ()) reachable |= transports;
					break;
				default:;
				}
				supported |= transports;
			}
			reachable |= published;
		}

		// A dual-stack firewalled NTCP2 endpoint is one RouterAddress with caps
		// "46" in Java (setOutboundNTCP2Address). Locally we keep two address
		// objects so host/port mutations stay family-local, but merge them back
		// on the wire when everything except the family cap is identical. This
		// preserves the bytes our RouterInfo published before the split.
		bool IsMergeableNTCP2Twin (const RouterInfo::Address & v4, const RouterInfo::Address & v6)
		{
			if (!v4.IsNTCP2 () || !v6.IsNTCP2 () || v4.published || v6.published) return false;
			if (!v4.host.is_unspecified () || !v6.host.is_unspecified ()) return false;
			if (v4.port != v6.port || v4.date != v6.date || v4.v != v6.v || v4.pqVersions != v6.pqVersions)
				return false;
			if (v4.s != v6.s) return false;
			// Only the first 16 bytes of i are the NTCP2 IV; the tail of the
			// Tag is unused and must not affect comparison.
			if (std::memcmp (v4.i.data (), v6.i.data (), 16) != 0) return false;
			constexpr uint8_t familyMask = RouterInfo::AddressCaps::eV4 | RouterInfo::AddressCaps::eV6;
			if ((v4.caps & familyMask) != RouterInfo::AddressCaps::eV4) return false;
			if ((v6.caps & familyMask) != RouterInfo::AddressCaps::eV6) return false;
			return (v4.caps & ~familyMask) == (v6.caps & ~familyMask);
		}
	}

	RouterInfo::Buffer::Buffer (const uint8_t * buf, size_t len)
	{
		if (len > size ()) len = size ();
		memcpy (data (), buf, len);
		m_BufferLen = len;
	}

	RouterInfo::RouterInfo ()
		: m_FamilyID (0)
		, m_Buffer (nullptr)
		, m_Timestamp (0)
		, m_IsUpdated (false)
		, m_IsUnreachable (false)
		, m_IsFloodfill (false)
		, m_IsBufferScheduledToDelete (false)
		, m_SupportedTransports (0)
		, m_ReachableTransports (0)
		, m_PublishedTransports (0)
		, m_Caps (0)
		, m_BandwidthCap (0)
		, m_Version (0)
		, m_Congestion (eLowCongestion)
	{
		m_Addresses = AddressesPtr (new Addresses ()); // create empty list
	}

	RouterInfo::RouterInfo (const std::string & fullPath)
		: m_FamilyID (0)
		, m_Buffer (nullptr)
		, m_Timestamp (0)
		, m_IsUpdated (false)
		, m_IsUnreachable (false)
		, m_IsFloodfill (false)
		, m_IsBufferScheduledToDelete (false)
		, m_SupportedTransports (0)
		, m_ReachableTransports (0)
		, m_PublishedTransports (0)
		, m_Caps (0)
		, m_BandwidthCap (0)
		, m_Version (0)
		, m_Congestion (eLowCongestion)
	{
		m_Addresses = AddressesPtr (new Addresses ()); // create empty list
		m_Buffer = RouterInfo::NewBuffer (); // always RouterInfo's
		ReadFromFile (fullPath);
	}

	RouterInfo::RouterInfo (std::shared_ptr<Buffer> && buf, size_t len)
		: m_FamilyID (0)
		, m_Buffer (nullptr)
		, m_Timestamp (0)
		, m_IsUpdated (true)
		, m_IsUnreachable (false)
		, m_IsFloodfill (false)
		, m_IsBufferScheduledToDelete (false)
		, m_SupportedTransports (0)
		, m_ReachableTransports (0)
		, m_PublishedTransports (0)
		, m_Caps (0)
		, m_BandwidthCap (0)
		, m_Version (0)
		, m_Congestion (eLowCongestion)
	{
		if (len <= MAX_RI_BUFFER_SIZE)
		{
			m_Addresses = AddressesPtr (new Addresses ()); // create empty list
			m_Buffer = buf;
			if (m_Buffer) m_Buffer->SetBufferLen (len);
			ReadFromBuffer (true);
		}
		else
		{
			LogPrint (eLogError, "RouterInfo: Buffer is too long ", len, ". Ignored");
			m_Buffer = nullptr;
			// Keep GetAddresses() non-null even for a rejected buffer.
			m_Addresses = AddressesPtr (new Addresses ());
			m_IsUnreachable = true;
		}
	}

	RouterInfo::RouterInfo (const uint8_t * buf, size_t len)
		: RouterInfo (netdb.NewRouterInfoBuffer (buf, len), len)
	{
	}

	RouterInfo::~RouterInfo () { }

	bool RouterInfo::Update (const uint8_t * buf, size_t len)
	{
		if (!buf || len > MAX_RI_BUFFER_SIZE)
		{
			LogPrint (eLogWarning, "RouterInfo: Updated buffer is too long ", len, ". Not changed");
			return false;
		}
		auto identity = m_RouterIdentity;
		if (!identity)
		{
			LogPrint (eLogWarning, "RouterInfo: Can't update without an identity");
			return false;
		}
		// Verify the signature with the identity we already have. This binds the
		// new body to the cached router identity; the caller also checks that
		// the announced identity hash matches the store key.
		size_t signatureLen = identity->GetSignatureLen ();
		if (len < signatureLen)
		{
			LogPrint (eLogWarning, "RouterInfo: Updated buffer is shorter than the signature");
			return false;
		}
		size_t signedLen = len - signatureLen;
		if (!identity->Verify (buf, signedLen, buf + signedLen))
		{
			LogPrint (eLogWarning, "RouterInfo: Updated signature verification failed. Not changed");
			return false;
		}
		size_t identityLen = identity->GetFullLen ();
		if (identityLen >= signedLen)
		{
			LogPrint (eLogError, "RouterInfo: Identity length ", identityLen, " leaves no signed body");
			return false;
		}
		// ReadFromBuffer() commits its result only after the complete body has
		// been validated. A signed but malformed update therefore leaves the
		// previously cached RouterInfo untouched instead of destroying it
		// (Java NetDb keeps the old RouterInfo when validation fails).
		if (!ReadFromBuffer (buf + identityLen, signedLen - identityLen))
		{
			LogPrint (eLogError, "RouterInfo: Updated RouterInfo is malformed. Not changed");
			return false;
		}
		// Install the validated body first; publish the updated flag only after
		// it is visible. A persistence pass can then never snapshot the old
		// body while consuming the new update flag.
		UpdateBuffer (buf, len);
		m_IsUpdated.store (true);
		return true;
	}

	void RouterInfo::SetRouterIdentity (std::shared_ptr<const IdentityEx> identity)
	{
		m_RouterIdentity = std::move (identity);
		m_Timestamp.store (i2p::util::GetMillisecondsSinceEpoch ());
	}

	bool RouterInfo::LoadFile (const std::string & fullPath)
	{
		std::ifstream s (fullPath, std::ifstream::binary);
		if (s.is_open ())
		{
			s.seekg (0, std::ios::end);
			auto end = s.tellg ();
			if (end < 0)
			{
				LogPrint (eLogError, "RouterInfo: Can't determine size of file ", fullPath);
				return false;
			}
			size_t bufferLen = static_cast<size_t> (end);
			if (bufferLen < 40 || bufferLen > MAX_RI_BUFFER_SIZE)
			{
				LogPrint (eLogError, "RouterInfo: File ", fullPath, " is malformed");
				return false;
			}
			s.seekg (0, std::ios::beg);
			if (!m_Buffer) m_Buffer = NewBuffer ();
			if (!m_Buffer) return false;
			s.read ((char *)m_Buffer->data (), bufferLen);
			// A short read would leave the uninitialized tail of the pooled
			// buffer in m_Buffer and feed it to the parser.
			if (!s || static_cast<size_t> (s.gcount ()) != bufferLen)
			{
				LogPrint (eLogError, "RouterInfo: Short read from ", fullPath);
				m_Buffer->SetBufferLen (0);
				return false;
			}
			m_Buffer->SetBufferLen (bufferLen);
		}
		else
		{
			LogPrint (eLogError, "RouterInfo: Can't open file ", fullPath);
			return false;
		}
		return true;
	}

	void RouterInfo::ReadFromFile (const std::string & fullPath)
	{
		if (LoadFile (fullPath))
			// Files are locally stored but can still be corrupted or replaced.
			// Java PersistentDataStore.read() uses readBytes(in, true), so verify
			// the signature on load instead of trusting the data directory.
			ReadFromBuffer (true);
		else
			m_IsUnreachable.store (true);
	}

	void RouterInfo::ReadFromBuffer (bool verifySignature)
	{
		if (!m_Buffer)
		{
			m_IsUnreachable.store (true);
			return;
		}
		size_t bufferLen = m_Buffer->GetBufferLen ();
		m_RouterIdentity = NewIdentity (m_Buffer->data (), bufferLen);
		if (!m_RouterIdentity)
		{
			m_IsUnreachable.store (true);
			return;
		}
		size_t identityLen = m_RouterIdentity->GetFullLen ();
		if (identityLen >= bufferLen)
		{
			LogPrint (eLogError, "RouterInfo: Identity length ", identityLen, " exceeds buffer size ", bufferLen);
			m_IsUnreachable.store (true);
			return;
		}
		size_t signatureLen = m_RouterIdentity->GetSignatureLen ();
		if (signatureLen > bufferLen - identityLen)
		{
			LogPrint (eLogError, "RouterInfo: Signature length ", signatureLen, " exceeds buffer size ", bufferLen);
			m_IsUnreachable.store (true);
			return;
		}
		size_t signedLen = bufferLen - signatureLen;
		if (verifySignature)
		{
			// RSA types 4/5/6 are legacy but Java can still publish them.
			// IdentityEx::Verify now selects the raw PKCS#1 v1.5 verifier;
			// unsupported/zero key material remains fail-closed there.
			if (!m_RouterIdentity->Verify (
					(uint8_t *)m_Buffer->data (), signedLen, (uint8_t *)m_Buffer->data () + signedLen))
			{
				LogPrint (eLogError, "RouterInfo: Signature verification failed");
				m_IsUnreachable.store (true);
				return;
			}
		}
		// parse RI without its trailing signature
		if (identityLen >= signedLen)
		{
			LogPrint (eLogError, "RouterInfo: Identity length ", identityLen, " leaves no signed body");
			m_IsUnreachable.store (true);
			return;
		}
		if (!ReadFromBuffer (m_Buffer->data () + identityLen, signedLen - identityLen))
		{
			LogPrint (eLogError, "RouterInfo: Malformed message");
			m_IsUnreachable.store (true);
		}
	}

	bool RouterInfo::ReadFromBuffer (const uint8_t * buf, size_t len)
	{
		// Callers pass the signed body only: ReadFromBuffer(bool) and Update()
		// already removed the identity prefix and the trailing signature. The
		// parser must not subtract the signature length a second time, otherwise
		// the property section cannot end at len and every RouterInfo is rejected.
		if (!buf || len < 9) return false;
		// Parse into locals and commit only after the complete body is valid.
		// Update() must keep the previously cached RouterInfo when a signed but
		// malformed update arrives (Java NetDb keeps the old entry too).
		uint64_t timestamp = bufbe64toh (buf);
		size_t offset = 8; // timestamp
		// read addresses
		auto addresses = NewAddresses ();
		if (!addresses) return false;
		uint8_t numAddresses = buf[offset++];
		// Java RouterInfo.readBytes() rejects more than MAX_ADDRESSES (16)
		// before parsing any address. Keep the same bound so a malformed RI
		// cannot make us iterate over hundreds of empty address entries.
		if (numAddresses > MAX_RI_ADDRESSES) return false;
		for (int i = 0; i < numAddresses; i++)
		{
			if (offset > len || len - offset < 9) return false; // 1 byte cost + 8 bytes date
			uint8_t addressTransports = 0;
			auto address = NewAddress ();
			if (!address) return false;
			offset++; // cost, ignore
			address->date = bufbe64toh (buf + offset);
			offset += 8; // date
			bool isHost = false, isStaticKey = false, isV2 = false, isIntroKey = false;
			bool validString = false;
			auto transportStyle = ExtractString (buf + offset, len - offset, validString);
			if (!validString) return false;
			offset += transportStyle.length () + 1;
			// Java RouterAddress keeps unknown transport style strings as-is and
			// the transports only match the exact names. Accept the two known
			// spellings for each transport (the legacy names are still parsed so
			// an old cached entry stays syntactically valid), but not a prefix
			// such as "SSUfoo".
			if (transportStyle == "NTCP" || transportStyle == "NTCP2")
				address->transportStyle = eTransportNTCP2;
			else if (transportStyle == "SSU" || transportStyle == "SSU2")
			{
				address->transportStyle = eTransportSSU2;
				address->ssu.reset (new SSUExt ());
				address->ssu->mtu = 0;
			}
			else
				address->transportStyle = eTransportUnknown;
			address->caps = 0;
			address->port = 0;
			if (offset > len || len - offset < 2) return false;
			uint16_t size = bufbe16toh (buf + offset);
			offset += 2; // size
			// Bound the declared options block against the remaining RouterInfo,
			// not against the whole buffer. All address/property offsets below
			// stay within [0, size].
			if (size > len - offset) return false;
			size_t addressEnd = offset + size;
			size_t r = 0;
			std::set<std::string_view> keys;
			while (r < size)
			{
				auto [key, value, sz] = ExtractParam (buf + offset, size - r);
				// ExtractParam returns 0 for a malformed key/value/semicolon
				// sequence. Never let a parameter advance past its own block.
				if (!sz || sz > size - r) return false;
				r += sz;
				offset += sz;
				if (key.empty ())
				{
					LogPrint (eLogWarning, "RouterInfo: Empty address parameter key");
					return false;
				}
				// Java DataHelper.readProperties() rejects duplicate keys and
				// enforces ordering. We accept unsorted keys for tolerance but
				// reject duplicates so conflicting host/port/s/i/v/pq values
				// cannot overwrite each other.
				if (!keys.insert (key).second)
				{
					LogPrint (eLogWarning, "RouterInfo: Duplicate address parameter ", key);
					return false;
				}
				if (key == "host")
				{
					boost::system::error_code ecode;
					address->host = boost::asio::ip::make_address (value, ecode);
					if (!ecode && !address->host.is_unspecified ())
					{
						if (!i2p::transport::transports.IsInReservedRange (address->host)
							|| i2p::util::net::IsYggdrasilAddress (address->host))
							isHost = true;
						else
							// we consider such address as invalid
							address->transportStyle = eTransportUnknown;
					}
				}
				else if (key == "port")
				{
					int port = 0;
					auto res = std::from_chars (value.data (), value.data () + value.size (), port);
					if (res.ec != std::errc () || res.ptr != value.data () + value.size () || port < 0 || port > 65535)
					{
						LogPrint (eLogWarning, "RouterInfo: Invalid 'port' value ", value);
						address->port = 0;
					}
					else
						address->port = port;
				}
				else if (key == "mtu")
				{
					if (address->ssu)
					{
						int mtu = 0;
						auto res = std::from_chars (value.data (), value.data () + value.size (), mtu);
						if (res.ec != std::errc () || res.ptr != value.data () + value.size () || mtu < 0
							|| mtu > 65535)
						{
							LogPrint (eLogWarning, "RouterInfo: Invalid 'mtu' value ", value);
							address->ssu->mtu = 0;
						}
						else
							address->ssu->mtu = mtu;
					}
					else
						LogPrint (eLogWarning, "RouterInfo: Unexpected field 'mtu' for NTCP2");
				}
				else if (key == "caps")
					address->caps = ExtractAddressCaps (value);
				else if (key == "s") // ntcp2 or ssu2 static key
				{
					if (Base64ToByteStream (value, address->s, 32) == 32
						&& !(address->s[31] & 0x80)) // check if x25519 public key
						isStaticKey = true;
					else
						address->transportStyle = eTransportUnknown; // invalid address
				}
				else if (key == "i") // ntcp2 iv or ssu2 intro
				{
					if (address->IsNTCP2 ())
					{
						if (Base64ToByteStream (value, address->i, 16) == 16)
							address->published = true; // presence of "i" means "published" NTCP2
						else
							address->transportStyle = eTransportUnknown; // invalid address
					}
					else if (address->IsSSU2 ())
					{
						if (Base64ToByteStream (value, address->i, 32) == 32)
							isIntroKey = true;
						else
							address->transportStyle = eTransportUnknown; // invalid address
					}
				}
				else if (key == "v")
				{
					// The SSU2 spec permits a comma-separated compatibility list
					// (for example "2,3"). The base version that this transport
					// understands must still be 2; newer values are negotiated
					// through the "pq" option, not by treating v=3/4 as PQ.
					bool foundV2 = false, validList = true;
					size_t start = 0;
					while (start <= value.size ())
					{
						size_t end = value.find (',', start);
						if (end == std::string_view::npos) end = value.size ();
						auto token = value.substr (start, end - start);
						if (token.empty ())
							validList = false;
						else
						{
							for (char ch : token)
							{
								if (ch < '0' || ch > '9')
								{
									validList = false;
									break;
								}
							}
							if (token == "2") foundV2 = true;
						}
						if (!validList || end == value.size ()) break;
						start = end + 1;
					}
					if (validList && foundV2)
						isV2 = true;
					else
					{
						LogPrint (eLogWarning, "RouterInfo: Unexpected value ", value, " for v");
						address->transportStyle = eTransportUnknown; // invalid address
					}
				}
				else if (key == "pq")
				{
					if (address->transportStyle == eTransportNTCP2)
					{
						// Java publishes NTCP2 as a comma-separated preference
						// list, e.g. "5,4,3". Keep the strongest value we
						// understand instead of silently downgrading. Parse
						// whole tokens so random digits in a malformed value
						// cannot select a hybrid version.
						int best = 0;
						size_t start = 0;
						while (start <= value.size ())
						{
							size_t end = value.find (',', start);
							if (end == std::string_view::npos) end = value.size ();
							auto token = value.substr (start, end - start);
#if OPENSSL_PQ
							if ((token == "3" || token == "4" || token == "5")
								&& i2p::crypto::CanPublishPQ (token[0] - '0'))
								best = std::max (best, token[0] - '0');
#endif
							if (end == value.size ()) break;
							start = end + 1;
						}
						if (best) address->v = best;
					}
					else if (address->transportStyle == eTransportSSU2)
					{
						// Validate the list and remember every supported SSU2
						// hybrid version. The final selection is made after the
						// options loop, when "mtu" is known regardless of order.
						size_t start = 0;
						while (start <= value.size ())
						{
							size_t end = value.find (',', start);
							if (end == std::string_view::npos) end = value.size ();
							auto token = value.substr (start, end - start);
							if (token == "3" || token == "4") address->pqVersions |= (uint8_t)(1u << (token[0] - '0'));
							if (end == value.size ()) break;
							start = end + 1;
						}
					}
				}
				else if (key[0] == 'i')
				{
					// introducers
					if (!address->ssu)
					{
						LogPrint (eLogError, "RouterInfo: Introducer is presented for non-SSU address. Skipped");
						continue;
					}
					char indexChar = key.back ();
					if (indexChar < '0' || indexChar >= '0' + MAX_RI_INTRODUCERS)
					{
						// SSU2 spec: introducer options are ih[0-2], iexp[0-2],
						// itag[0-2]. Ignore unknown indices rather than
						// growing the introducer vector.
						LogPrint (eLogWarning, "RouterInfo: Invalid introducer index in ", key, " skipped");
						continue;
					}
					unsigned char index = indexChar - '0';
					auto key1 = key.substr (0, key.length () - 1);
					if (key1 != "itag" && key1 != "ih" && key1 != "iexp")
						continue; // unknown i* option, not an introducer field
					if (index >= address->ssu->introducers.size ())
					{
						if (address->ssu->introducers.empty ()) // first time
							address->ssu->introducers.reserve (MAX_RI_INTRODUCERS);
						address->ssu->introducers.resize (index + 1);
					}
					Introducer & introducer = address->ssu->introducers.at (index);
					if (key1 == "itag")
					{
						auto res = std::from_chars (value.data (), value.data () + value.size (), introducer.iTag);
						if (res.ec != std::errc () || res.ptr != value.data () + value.size ())
							LogPrint (eLogWarning, "RouterInfo: Invalid 'itag' value ", value);
					}
					else if (key1 == "ih")
					{
						if (Base64ToByteStream (value, introducer.iH, 32) != 32)
						{
							introducer.iH.Fill (0);
							LogPrint (eLogWarning, "RouterInfo: Invalid 'ih' value");
						}
					}
					else if (key1 == "iexp")
					{
						auto res = std::from_chars (value.data (), value.data () + value.size (), introducer.iExp);
						if (res.ec != std::errc () || res.ptr != value.data () + value.size ())
							LogPrint (eLogWarning, "RouterInfo: Invalid 'iexp' value ", value);
					}
				}
			}
			if (offset != addressEnd) return false;
			// Proposal 133 / Java UDPAddress: iexp is optional. An introducer
			// without an expiration never expires; represent that with the
			// largest uint32 so the session layer (which compares ts < iExp)
			// keeps using it. Treating a missing iexp as zero made a valid
			// firewalled SSU2 router unreachable.
			if (address->ssu)
			{
				for (auto & introducer : address->ssu->introducers)
					if (introducer.iTag && !introducer.iExp) introducer.iExp = std::numeric_limits<uint32_t>::max ();
			}
			if (address->transportStyle == eTransportSSU2 && address->pqVersions)
			{
#if OPENSSL_PQ
				// Java UDPTransport.getSSUVersion(): prefer pq=4 only when both
				// the published MTU and the local MTU can carry ML-KEM-768.
				// The local MTU is checked by SSU2Session::SetVersion().
				int best = 0;
				if ((address->pqVersions & (1u << 4)) && i2p::crypto::CanPublishPQ (4))
				{
					bool ipv6 = !address->host.is_unspecified () ? address->host.is_v6 ()
																 : ((address->caps & AddressCaps::eV6) != 0);
					int minMtu = ipv6 ? MIN_SSU2_MLKEM768_MTU_IPV6 : MIN_SSU2_MLKEM768_MTU_IPV4;
					int mtu = address->ssu ? address->ssu->mtu : 0;
					if (!mtu || mtu >= minMtu) best = 4;
				}
				// ML-KEM-512 is not provided by LibreSSL. Never select a
				// version that the local session cannot establish.
				if (!best && (address->pqVersions & (1u << 3)) && i2p::crypto::CanPublishPQ (3))
					best = 3;
				address->v = best ? best : 2;
#else
				// This build has no ML-KEM provider. Ignore a peer's pq hint
				// rather than selecting version 3/4 and failing every session.
				address->v = 2;
#endif
			}
			if (address->transportStyle == eTransportNTCP2)
			{
				if (isStaticKey && isV2)
				{
					if (isHost && address->port)
					{
						// Java NTCPTransport.getNTCP2Version() also requires the
						// "i" IV option and v containing "2". Without them the
						// address is not usable inbound and is not the outbound
						// form either (that one has no host/port), so reject it
						// instead of turning it into a fake non-published v4
						// endpoint.
						if (address->published)
						{
							if (address->host.is_v6 ())
								addressTransports
									|= (i2p::util::net::IsYggdrasilAddress (address->host) ? eNTCP2V6Mesh : eNTCP2V6);
							else
								addressTransports |= eNTCP2V4;
						}
					}
					else
					{
						address->published = false;
						if (address->caps)
						{
							if (address->caps & AddressCaps::eV4) addressTransports |= eNTCP2V4;
							if (address->caps & AddressCaps::eV6) addressTransports |= eNTCP2V6;
						}
						else
							addressTransports |= eNTCP2V4; // most likely, since we don't have host
					}
				}
			}
			else if (address->transportStyle == eTransportSSU2 && isV2 && isStaticKey && isIntroKey
				&& (!address->ssu || !address->ssu->mtu || address->ssu->mtu >= MIN_SSU2_MTU))
			{
				// SSU2 spec, MTU: below 1280 an address must not be used. Keep
				// the existing stricter policy of not accepting such an
				// address at all (test-ssu2-address-negotiation).
				if (isHost && address->port)
				{
					// A concrete endpoint's family is defined by its host. Address
					// caps are untrusted and must not make a published IPv4 address
					// selectable as IPv6 (or vice versa), which would make the
					// session layer connect to the wrong family.
					if (address->host.is_v4 ())
						addressTransports |= eSSU2V4;
					else if (address->host.is_v6 ())
						addressTransports |= eSSU2V6;
					address->published = true;
				}
				else
				{
					// Non-published addresses use caps to declare the families the
					// router can be reached on through introducers.
					if (address->IsV4 ()) addressTransports |= eSSU2V4;
					if (address->IsV6 ()) addressTransports |= eSSU2V6;
					if (address->ssu && !address->ssu->introducers.empty ())
					{
						// exclude invalid introducers
						uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
						UpdateIntroducers (address, ts);
					}
				}
			}
			if (addressTransports)
			{
				// A non-published address with both family caps is a single
				// RouterAddress entry but two independent endpoints. Give each
				// family its own object so host, reachability, published state,
				// introducers and MTU cannot leak across families. Keep the
				// first address for a transport, but prefer a later published
				// address over an earlier outbound-only one (Java keeps both and
				// its transports pick the usable one).
				bool split = address->host.is_unspecified ()
					&& ((address->IsNTCP2 () && (addressTransports & eNTCP2V4) && (addressTransports & eNTCP2V6))
						|| (address->IsSSU2 () && (addressTransports & eSSU2V4) && (addressTransports & eSSU2V6)));
				for (uint8_t idx = 0; idx < eNumTransports; idx++)
				{
					if (!((1u << idx) & addressTransports)) continue;
					bool isV4Slot = (idx == eNTCP2V4Idx || idx == eSSU2V4Idx);
					bool isFamilySlot = address->IsNTCP2 () ? (idx == eNTCP2V4Idx || idx == eNTCP2V6Idx)
															: (idx == eSSU2V4Idx || idx == eSSU2V6Idx);
					std::shared_ptr<Address> candidate = address;
					if (split && isFamilySlot)
					{
						auto copy = NewAddress ();
						if (!copy) continue;
						CopyAddressForFamily (*address, *copy, isV4Slot);
						candidate = copy;
					}
					auto & slot = (*addresses)[idx];
					if (!slot || (candidate->published && !slot->published)) slot = candidate;
				}
			}
		}
		// Derive the transport masks from the surviving address objects, the
		// same way UpdateSupportedTransports() does. Keeping one implementation
		// avoids the parser and later local updates disagreeing.
		CompatibleTransports supportedTransports = 0, reachableTransports = 0, publishedTransports = 0;
		ComputeAddressTransports (*addresses, supportedTransports, reachableTransports, publishedTransports);
		// read peers
		if (offset > len || len - offset < 1) return false;
		uint8_t numPeers = buf[offset];
		offset++; // num peers
		if (numPeers > (len - offset) / 32) return false;
		offset += (size_t)numPeers * 32; // TODO: read peers
		// read properties
		if (offset > len || len - offset < 2) return false;
		int version = 0;
		bool isNetId = false;
		bool valid = true;
		std::string family;
		std::string familySignature;
		bool hasFamilySignature = false;
		uint8_t caps = 0;
		char bandwidthCap = 0;
		Congestion congestion = eLowCongestion;
		// Only LocalRouterInfo keeps a property map; don't allocate a second
		// copy of every remote RouterInfo's properties just to discard it.
		const bool storesProperties = StoresProperties ();
		std::vector<std::pair<std::string, std::string>> properties;
		if (storesProperties) properties.reserve (16);
		uint16_t size = bufbe16toh (buf + offset);
		offset += 2; // size
		if (size > len - offset) return false;
		size_t r = 0;
		std::set<std::string_view> propertyKeys;
		while (r < size)
		{
			auto [key, value, sz] = ExtractParam (buf + offset, size - r);
			if (!sz || sz > size - r) return false;
			r += sz;
			offset += sz;
			if (key.empty ())
			{
				LogPrint (eLogWarning, "RouterInfo: Empty property key");
				return false;
			}
			// Java reads RouterInfo properties with readProperties(..., true),
			// which rejects duplicate keys (and unsorted input). Keep accepting
			// unsorted input but do not let a later duplicate override an
			// earlier router.version/netId/caps/family value.
			if (!propertyKeys.insert (key).second)
			{
				LogPrint (eLogWarning, "RouterInfo: Duplicate property ", key);
				return false;
			}
			if (storesProperties) properties.emplace_back (std::string (key), std::string (value));

			// extract caps
			if (key == "caps") ExtractCaps (value, caps, bandwidthCap, congestion);
			// extract version
			else if (key == ROUTER_INFO_PROPERTY_VERSION)
				version = ParseRouterVersion (value);
			// check netId
			else if (key == ROUTER_INFO_PROPERTY_NETID)
			{
				isNetId = true;
				int netID;
				auto res = std::from_chars (value.data (), value.data () + value.size (), netID);
				if (res.ec != std::errc () || res.ptr != value.data () + value.size ()
					|| netID != i2p::context.GetNetID ())
				{
					LogPrint (eLogError, "RouterInfo: Unexpected ", ROUTER_INFO_PROPERTY_NETID, "=", value);
					valid = false;
				}
			}
			// family
			else if (key == ROUTER_INFO_PROPERTY_FAMILY)
			{
				family.assign (value);
				boost::to_lower (family);
			}
			else if (key == ROUTER_INFO_PROPERTY_FAMILY_SIG)
			{
				familySignature.assign (value);
				hasFamilySignature = true;
			}
		}

		// With the signature excluded from len, the property mapping must end
		// exactly at the end of the signed body. Java reads the signature
		// immediately after readProperties(); trailing bytes would shift it.
		if (offset != len)
		{
			LogPrint (eLogWarning, "RouterInfo: Malformed property section, unexpected trailing bytes");
			return false;
		}

		// Verify the family signature with the final family value. Java reads
		// the option map first and FamilyKeyCrypto.verify(ri) afterwards, so a
		// tolerated unsorted "family.sig" before "family" must not disable the
		// family binding.
		FamilyID familyID = 0;
		if (hasFamilySignature)
		{
			if (netdb.GetFamilies ().VerifyFamily (family, GetIdentHash (), familySignature)) // TODO
				familyID = netdb.GetFamilies ().GetFamilyID (family);
			else
			{
				LogPrint (eLogWarning, "RouterInfo: Family ", family, " signature verification failed");
				valid = false;
			}
		}

		// Java accepts a fresh signature-valid RouterInfo with no addresses
		// (or only an address style it does not use) for up to 54 minutes;
		// the no-address policy is applied by NetDb::IsRouterInfoStoreValid.
		if (!isNetId || !version) valid = false;
		if (!valid) return false;

		// Java routers older than the peer-test version must not publish the
		// SSU2 "B" cap. Apply it after all properties are known so an unsorted
		// caps/version pair cannot re-add it.
		if (version < NETDB_MIN_PEER_TEST_VERSION)
		{
			for (auto & address : *addresses)
				if (address && address->IsSSU2 ()) address->caps &= ~eSSUTesting;
		}

		// Commit the fully validated body. Nothing above changes this object's
		// state, so Update() can keep the old RouterInfo on any failure.
		m_Timestamp.store (timestamp);
		m_Caps.store (caps);
		m_BandwidthCap.store (bandwidthCap);
		m_Congestion.store (congestion);
		m_FamilyID.store (familyID);
		m_SupportedTransports.store (supportedTransports);
		m_ReachableTransports.store (reachableTransports);
		m_PublishedTransports.store (publishedTransports);
		m_Version.store (version);
		m_Addresses.Store (std::move (addresses));
		m_IsFloodfill.store ((caps & eFloodfill) != 0);
		m_IsUnreachable.store (false);
		ClearProperties ();
		for (const auto & property : properties) SetProperty (property.first, property.second);

		return true;
	}

	void RouterInfo::ExtractCaps (
		std::string_view value, uint8_t & caps, char & bandwidthCap, Congestion & congestion) const
	{
		// Java RouterInfo.getBandwidthTier() searches the fixed strongest-first
		// list "X P O N M L K", i.e. the result does not depend on the order in
		// which several bandwidth caps appear. Track the strongest one here and
		// derive the high/extra flags from it instead of letting the last
		// character win (which could keep eExtraBandwidth with an "O" tier).
		int bandwidthRank = BandwidthRank (bandwidthCap);
		for (auto cap : value)
		{
			switch (cap)
			{
			case CAPS_FLAG_FLOODFILL: caps |= Caps::eFloodfill; break;
			case CAPS_FLAG_LOW_BANDWIDTH1:
			case CAPS_FLAG_LOW_BANDWIDTH2:
			case CAPS_FLAG_LOW_BANDWIDTH3:
			case CAPS_FLAG_LOW_BANDWIDTH4:
			case CAPS_FLAG_HIGH_BANDWIDTH:
			case CAPS_FLAG_EXTRA_BANDWIDTH1:
			case CAPS_FLAG_EXTRA_BANDWIDTH2:
			{
				int rank = BandwidthRank (cap);
				if (rank > bandwidthRank)
				{
					bandwidthRank = rank;
					bandwidthCap = cap;
				}
				break;
			}
			case CAPS_FLAG_HIDDEN: caps |= Caps::eHidden; break;
			case CAPS_FLAG_REACHABLE: caps |= Caps::eReachable; break;
			case CAPS_FLAG_UNREACHABLE: caps |= Caps::eUnreachable; break;
			// Several congestion caps in one string are malformed, but pick the
			// most severe one independent of their order.
			case CAPS_FLAG_MEDIUM_CONGESTION:
				if (congestion < eMediumCongestion) congestion = eMediumCongestion;
				break;
			case CAPS_FLAG_HIGH_CONGESTION:
				if (congestion < eHighCongestion) congestion = eHighCongestion;
				break;
			case CAPS_FLAG_REJECT_ALL_CONGESTION: congestion = eRejectAll; break;
			default:;
			}
		}
		caps &= ~(Caps::eHighBandwidth | Caps::eExtraBandwidth);
		if (bandwidthRank >= 5) caps |= Caps::eHighBandwidth;
		if (bandwidthRank >= 6) caps |= Caps::eExtraBandwidth;
		bandwidthCap = BandwidthCapForRank (bandwidthRank);
	}

	uint8_t RouterInfo::ExtractAddressCaps (std::string_view value) const
	{
		uint8_t caps = 0;
		for (auto cap : value)
		{
			switch (cap)
			{
			case CAPS_FLAG_V4: caps |= AddressCaps::eV4; break;
			case CAPS_FLAG_V6: caps |= AddressCaps::eV6; break;
			case CAPS_FLAG_SSU2_TESTING: caps |= AddressCaps::eSSUTesting; break;
			case CAPS_FLAG_SSU2_INTRODUCER: caps |= AddressCaps::eSSUIntroducer; break;
			default:;
			}
		}
		return caps;
	}

	void RouterInfo::UpdateIntroducers (std::shared_ptr<Address> address, uint64_t ts)
	{
		if (!address || !address->ssu) return;
		int numValid = 0;
		for (auto & it : address->ssu->introducers)
		{
			// Proposal 133: iexp is optional. A missing expiration means the
			// introducer does not expire (Java UDPAddress uses exp == 0).
			if (it.iTag && !it.iExp) it.iExp = std::numeric_limits<uint32_t>::max ();
			if (it.iTag && ts < it.iExp && !it.iH.IsZero ())
				numValid++;
			else
				it.iTag = 0;
		}
		if (!numValid) address->ssu->introducers.resize (0);
	}

	bool RouterInfo::IsNewer (const uint8_t * buf, size_t len) const
	{
		auto identity = m_RouterIdentity;
		if (!identity || !buf) return false;
		size_t size = identity->GetFullLen ();
		if (size + 8 > len) return false;
		return bufbe64toh (buf + size) > m_Timestamp.load ();
	}

	const uint8_t * RouterInfo::LoadBuffer (const std::string & fullPath)
	{
		std::lock_guard<std::mutex> l (m_BufferMutex);
		if (!m_Buffer)
		{
			if (LoadFile (fullPath))
				LogPrint (eLogDebug, "RouterInfo: Buffer for ", GetIdentHashAbbreviation (GetIdentHash ()),
					" loaded from file");
			else
			{
				// LoadFile may have allocated a zero-length buffer before the
				// short-read/length check failed. Leave it null so a later
				// call can retry instead of returning a pointer with len 0.
				m_Buffer = nullptr;
				return nullptr;
			}
		}
		return m_Buffer->data ();
	}

	bool RouterInfo::SaveToFile (const std::string & fullPath, std::shared_ptr<Buffer> buf)
	{
		if (!buf) return false;
		// Replace the previous descriptor atomically. A crash or short write
		// must not truncate the last valid cached RouterInfo.
		const std::string tempPath = fullPath + ".tmp";
		{
			std::ofstream f (tempPath, std::ofstream::binary | std::ofstream::out | std::ofstream::trunc);
			if (!f.is_open ())
			{
				LogPrint (eLogError, "RouterInfo: Can't save to ", tempPath);
				return false;
			}
			f.write ((char *)buf->data (), buf->GetBufferLen ());
			f.flush ();
			if (!f)
			{
				LogPrint (eLogError, "RouterInfo: Short write to ", tempPath);
				f.close ();
				i2p::fs::Remove (tempPath);
				return false;
			}
		}
		if (!i2p::fs::Rename (tempPath, fullPath))
		{
			LogPrint (eLogError, "RouterInfo: Can't replace ", fullPath);
			i2p::fs::Remove (tempPath);
			return false;
		}
		return true;
	}

	bool RouterInfo::SaveToFile (const std::string & fullPath)
	{
		if (m_IsUnreachable.load ()) return false; // don't save bad router
		std::shared_ptr<Buffer> buffer;
		{
			std::lock_guard<std::mutex> l (m_BufferMutex);
			buffer = m_Buffer;
		}
		if (!buffer)
		{
			LogPrint (eLogWarning, "RouterInfo: Can't save, m_Buffer == NULL");
			return false;
		}
		return SaveToFile (fullPath, buffer);
	}

	std::string_view RouterInfo::ExtractString (const uint8_t * buf, size_t len, bool & valid) const
	{
		valid = false;
		if (!len) return { };
		uint8_t l = buf[0];
		if (l > len - 1)
		{
			LogPrint (eLogWarning, "RouterInfo: String length ", (int)l, " exceeds buffer size ", len);
			return { };
		}
		valid = true;
		return { (const char *)(buf + 1), l };
	}

	std::tuple<std::string_view, std::string_view, size_t> RouterInfo::ExtractParam (
		const uint8_t * buf, size_t len) const
	{
		// The returned size is zero for any malformed parameter. Callers must
		// treat that as a hard parse error and must not advance by a clamped
		// length (which could desynchronize the address/property loop).
		bool valid = false;
		auto key = ExtractString (buf, len, valid);
		if (!valid) return { std::string_view (), std::string_view (), 0 };
		size_t offset = key.length () + 1;
		if (offset >= len) return { std::string_view (), std::string_view (), 0 };
		if (buf[offset] != '=')
		{
			LogPrint (eLogWarning, "RouterInfo: Unexpected character ", buf[offset], " instead '=' after ", key);
			return { std::string_view (), std::string_view (), 0 };
		}
		offset++;
		if (offset >= len) return { std::string_view (), std::string_view (), 0 };
		auto value = ExtractString (buf + offset, len - offset, valid);
		if (!valid) return { std::string_view (), std::string_view (), 0 };
		offset += value.length () + 1;
		if (offset >= len) return { std::string_view (), std::string_view (), 0 };
		if (buf[offset] != ';')
		{
			LogPrint (eLogWarning, "RouterInfo: Unexpected character ", buf[offset], " instead ';' after ", value);
			return { std::string_view (), std::string_view (), 0 };
		}
		offset++;
		return { key, value, offset };
	}

	void RouterInfo::AddNTCP2Address (const uint8_t * staticKey, const uint8_t * iv, int port, uint8_t caps)
	{
		// A firewalled NTCP2 address with caps "46" is one RouterAddress entry
		// but two independent per-family endpoints. Storing the same object in
		// both slots let UpdateAddress()/PublishNTCP2Address() set a v4 host on
		// the v6 object (and vice versa), silently dropping a family from our
		// own RouterInfo. Split it exactly like SSU2.
		auto addresses = GetAddresses ();
		auto add = [&] (bool v4)
		{
			auto addr = std::make_shared<Address> ();
			addr->port = port;
			addr->transportStyle = eTransportNTCP2;
			addr->caps = static_cast<uint8_t> (
				(caps & ~(AddressCaps::eV4 | AddressCaps::eV6)) | (v4 ? AddressCaps::eV4 : AddressCaps::eV6));
			addr->date = 0;
			addr->published = false;
			memcpy (addr->s, staticKey, 32);
			memcpy (addr->i, iv, 16);
			if (v4)
			{
				m_SupportedTransports.fetch_or (eNTCP2V4);
				(*addresses)[eNTCP2V4Idx] = addr;
			}
			else
			{
				m_SupportedTransports.fetch_or (eNTCP2V6);
				(*addresses)[eNTCP2V6Idx] = addr;
			}
		};
		bool v4 = caps & AddressCaps::eV4, v6 = caps & AddressCaps::eV6;
		if (v4 && v6)
		{
			add (true);
			add (false);
		}
		else if (v4)
			add (true);
		else if (v6)
			add (false);
	}

	void RouterInfo::AddNTCP2Address (
		const uint8_t * staticKey, const uint8_t * iv, const boost::asio::ip::address & host, int port)
	{
		auto addr = std::make_shared<Address> ();
		addr->host = host;
		addr->port = port;
		addr->transportStyle = eTransportNTCP2;
		addr->date = 0;
		addr->published = true;
		memcpy (addr->s, staticKey, 32);
		memcpy (addr->i, iv, 16);
		addr->caps = 0;
		if (host.is_unspecified ())
		{
			if (host.is_v4 ()) addr->caps |= eV4;
			if (host.is_v6 ()) addr->caps |= eV6;
		}
		auto addresses = GetAddresses ();
		// An unspecified host (0.0.0.0/::) is not a usable endpoint; keep it
		// supported (it carries the keys) but never claim it is reachable.
		const bool endpoint = !host.is_unspecified () && port;
		if (addr->IsV4 ())
		{
			m_SupportedTransports.fetch_or (eNTCP2V4);
			if (endpoint) m_ReachableTransports.fetch_or (eNTCP2V4);
			(*addresses)[eNTCP2V4Idx] = addr;
		}
		if (addr->IsV6 ())
		{
			if (i2p::util::net::IsYggdrasilAddress (addr->host))
			{
				m_SupportedTransports.fetch_or (eNTCP2V6Mesh);
				if (endpoint) m_ReachableTransports.fetch_or (eNTCP2V6Mesh);
				(*addresses)[eNTCP2V6MeshIdx] = addr;
			}
			else
			{
				m_SupportedTransports.fetch_or (eNTCP2V6);
				if (endpoint) m_ReachableTransports.fetch_or (eNTCP2V6);
				(*addresses)[eNTCP2V6Idx] = addr;
			}
		}
	}

	void RouterInfo::RemoveNTCP2Address (bool v4)
	{
		auto addresses = GetAddresses ();
		if (v4)
		{
			if ((*addresses)[eNTCP2V6Idx]) (*addresses)[eNTCP2V6Idx]->caps &= ~AddressCaps::eV4;
			(*addresses)[eNTCP2V4Idx].reset ();
		}
		else
		{
			if ((*addresses)[eNTCP2V4Idx]) (*addresses)[eNTCP2V4Idx]->caps &= ~AddressCaps::eV6;
			(*addresses)[eNTCP2V6Idx].reset ();
		}
		UpdateSupportedTransports ();
	}

	void RouterInfo::AddSSU2Address (const uint8_t * staticKey, const uint8_t * introKey, int port, uint8_t caps)
	{
		auto addresses = GetAddresses ();
		bool v4 = caps & AddressCaps::eV4, v6 = caps & AddressCaps::eV6;
		bool split = v4 && v6;
		auto add = [&] (bool isV4)
		{
			auto addr = std::make_shared<Address> ();
			addr->transportStyle = eTransportSSU2;
			addr->port = port;
			addr->caps = split ? (uint8_t)(isV4 ? AddressCaps::eV4 : AddressCaps::eV6) : caps;
			addr->date = 0;
			addr->ssu.reset (new SSUExt ());
			addr->ssu->mtu = 0;
			memcpy (addr->s, staticKey, 32);
			memcpy (addr->i, introKey, 32);
			if (isV4)
			{
				m_SupportedTransports |= eSSU2V4;
				(*addresses)[eSSU2V4Idx] = addr;
			}
			else
			{
				m_SupportedTransports |= eSSU2V6;
				(*addresses)[eSSU2V6Idx] = addr;
			}
		};
		if (split)
		{
			add (true);
			add (false);
		}
		else if (v4)
			add (true);
		else if (v6)
			add (false);
	}

	void RouterInfo::AddSSU2Address (
		const uint8_t * staticKey, const uint8_t * introKey, const boost::asio::ip::address & host, int port)
	{
		auto addr = std::make_shared<Address> ();
		addr->transportStyle = eTransportSSU2;
		addr->host = host;
		addr->port = port;
		addr->published = true;
		addr->date = 0;
		addr->ssu.reset (new SSUExt ());
		addr->ssu->mtu = 0;
		memcpy (addr->s, staticKey, 32);
		memcpy (addr->i, introKey, 32);
		if (!host.is_unspecified ())
			addr->caps = i2p::data::RouterInfo::eSSUTesting | i2p::data::RouterInfo::eSSUIntroducer; // BC;
		else
		{
			addr->caps = 0;
			if (host.is_v4 ()) addr->caps |= eV4;
			if (host.is_v6 ()) addr->caps |= eV6;
		}
		auto addresses = GetAddresses ();
		const bool endpoint = !host.is_unspecified () && port;
		if (addr->IsV4 ())
		{
			m_SupportedTransports.fetch_or (eSSU2V4);
			if (endpoint) m_ReachableTransports.fetch_or (eSSU2V4);
			(*addresses)[eSSU2V4Idx] = addr;
		}
		if (addr->IsV6 ())
		{
			m_SupportedTransports.fetch_or (eSSU2V6);
			if (endpoint) m_ReachableTransports.fetch_or (eSSU2V6);
			(*addresses)[eSSU2V6Idx] = addr;
		}
	}

	void RouterInfo::RemoveSSU2Address (bool v4)
	{
		auto addresses = GetAddresses ();
		if (v4)
		{
			if ((*addresses)[eSSU2V6Idx]) (*addresses)[eSSU2V6Idx]->caps &= ~AddressCaps::eV4;
			(*addresses)[eSSU2V4Idx].reset ();
		}
		else
		{
			if ((*addresses)[eSSU2V4Idx]) (*addresses)[eSSU2V4Idx]->caps &= ~AddressCaps::eV6;
			(*addresses)[eSSU2V6Idx].reset ();
		}
		UpdateSupportedTransports ();
	}

	bool RouterInfo::IsNTCP2 (bool v4only) const
	{
		auto transports = m_SupportedTransports.load ();
		return v4only ? (transports & eNTCP2V4) : (transports & (eNTCP2V4 | eNTCP2V6));
	}

	void RouterInfo::EnableV6 ()
	{
		if (!IsV6 ())
		{
			uint8_t addressCaps = AddressCaps::eV6;
			if (IsV4 ()) addressCaps |= AddressCaps::eV4;
			SetUnreachableAddressesTransportCaps (addressCaps);
			UpdateSupportedTransports ();
		}
	}

	void RouterInfo::EnableV4 ()
	{
		if (!IsV4 ())
		{
			uint8_t addressCaps = AddressCaps::eV4;
			if (IsV6 ()) addressCaps |= AddressCaps::eV6;
			SetUnreachableAddressesTransportCaps (addressCaps);
			UpdateSupportedTransports ();
		}
	}

	void RouterInfo::DisableV6 ()
	{
		if (IsV6 ())
		{
			auto addresses = GetAddresses ();
			if ((*addresses)[eNTCP2V6Idx])
			{
				if ((*addresses)[eNTCP2V6Idx]->IsV4 () && (*addresses)[eNTCP2V4Idx])
					(*addresses)[eNTCP2V4Idx]->caps &= ~AddressCaps::eV6;
				(*addresses)[eNTCP2V6Idx].reset ();
			}
			if ((*addresses)[eSSU2V6Idx])
			{
				if ((*addresses)[eSSU2V6Idx]->IsV4 () && (*addresses)[eSSU2V4Idx])
					(*addresses)[eSSU2V4Idx]->caps &= ~AddressCaps::eV6;
				(*addresses)[eSSU2V6Idx].reset ();
			}
			UpdateSupportedTransports ();
		}
	}

	void RouterInfo::DisableV4 ()
	{
		if (IsV4 ())
		{
			auto addresses = GetAddresses ();
			if ((*addresses)[eNTCP2V4Idx])
			{
				if ((*addresses)[eNTCP2V4Idx]->IsV6 () && (*addresses)[eNTCP2V6Idx])
					(*addresses)[eNTCP2V6Idx]->caps &= ~AddressCaps::eV4;
				(*addresses)[eNTCP2V4Idx].reset ();
			}
			if ((*addresses)[eSSU2V4Idx])
			{
				if ((*addresses)[eSSU2V4Idx]->IsV6 () && (*addresses)[eSSU2V6Idx])
					(*addresses)[eSSU2V6Idx]->caps &= ~AddressCaps::eV4;
				(*addresses)[eSSU2V4Idx].reset ();
			}
			UpdateSupportedTransports ();
		}
	}

	void RouterInfo::EnableMesh ()
	{
		if (!IsMesh ())
		{
			m_SupportedTransports |= eNTCP2V6Mesh;
			m_ReachableTransports |= eNTCP2V6Mesh;
		}
	}

	void RouterInfo::DisableMesh ()
	{
		if (IsMesh ())
		{
			m_SupportedTransports &= static_cast<CompatibleTransports> (~eNTCP2V6Mesh);
			m_ReachableTransports &= static_cast<CompatibleTransports> (~eNTCP2V6Mesh);
			(*GetAddresses ())[eNTCP2V6MeshIdx].reset ();
		}
	}

	std::shared_ptr<const RouterInfo::Address> RouterInfo::GetSSU2V4Address () const
	{
		return (*GetAddresses ())[eSSU2V4Idx];
	}

	std::shared_ptr<const RouterInfo::Address> RouterInfo::GetSSU2V6Address () const
	{
		return (*GetAddresses ())[eSSU2V6Idx];
	}

	std::shared_ptr<const RouterInfo::Address> RouterInfo::GetSSU2Address (bool v4) const
	{
		auto transports = m_SupportedTransports.load ();
		if (v4)
		{
			if (transports & eSSU2V4) return GetSSU2V4Address ();
		}
		else
		{
			if (transports & eSSU2V6) return GetSSU2V6Address ();
		}
		return nullptr;
	}

	RouterInfo::AddressesPtr RouterInfo::GetAddresses () const
	{
		return m_Addresses.Load ();
	}

	template <typename Filter> std::shared_ptr<const RouterInfo::Address> RouterInfo::GetAddress (Filter filter) const
	{
		auto addresses = GetAddresses ();
		for (const auto & address : *addresses)
			if (address && filter (address)) return address;
		return nullptr;
	}

	std::shared_ptr<const RouterInfo::Address> RouterInfo::GetNTCP2V4Address () const
	{
		return (*GetAddresses ())[eNTCP2V4Idx];
	}

	std::shared_ptr<const RouterInfo::Address> RouterInfo::GetNTCP2V6Address () const
	{
		return (*GetAddresses ())[eNTCP2V6Idx];
	}

	std::shared_ptr<const RouterInfo::Address> RouterInfo::GetPublishedNTCP2V4Address () const
	{
		auto addr = (*GetAddresses ())[eNTCP2V4Idx];
		if (addr && addr->IsPublishedNTCP2 ()) return addr;
		return nullptr;
	}

	std::shared_ptr<const RouterInfo::Address> RouterInfo::GetPublishedNTCP2V6Address () const
	{
		auto addr = (*GetAddresses ())[eNTCP2V6Idx];
		if (addr && addr->IsPublishedNTCP2 ()) return addr;
		return nullptr;
	}

	std::shared_ptr<const RouterInfo::Address> RouterInfo::GetYggdrasilAddress () const
	{
		return (*GetAddresses ())[eNTCP2V6MeshIdx];
	}

	std::shared_ptr<RouterProfile> RouterInfo::GetProfile () const
	{
		auto profile = m_Profile.Load ();
		if (!profile)
		{
			profile = GetRouterProfile (GetIdentHash ());
			// A concurrent caller may have installed one first; either object is
			// equivalent and the last store is harmless.
			m_Profile.Store (profile);
		}
		return profile;
	}

	bool RouterInfo::Encrypt (const uint8_t * data, uint8_t * encrypted) const
	{
		auto identity = m_RouterIdentity;
		if (!identity || !encrypted) return false;
		auto encryptor = identity->CreateEncryptor (nullptr);
		return encryptor && encryptor->Encrypt (data, encrypted);
	}

	bool RouterInfo::IsEligibleFloodfill () const
	{
		// floodfill must have published ipv4 or reachable ipv4 and published ipv6
		// >= 0.9.62 and not DSA
		auto identity = GetIdentity ();
		if (!identity) return false;
		return m_Version.load () >= NETDB_MIN_FLOODFILL_VERSION
			&& (IsPublished (true) || (IsReachableBy (eNTCP2V4 | eSSU2V4) && IsPublished (false)))
			&& identity->GetSigningKeyType () != SIGNING_KEY_TYPE_DSA_SHA1;
	}

	bool RouterInfo::IsPublished (bool v4) const
	{
		if ((m_Caps.load () & eUnreachable) || IsHidden ())
			return false; // if router sets U or is hidden we assume that all addresses are not published
		return IsPublishedOn (v4 ? (eNTCP2V4 | eSSU2V4) : (eNTCP2V6 | eSSU2V6));
	}

	bool RouterInfo::IsPublishedOn (CompatibleTransports transports) const
	{
		return m_PublishedTransports.load () & transports;
	}

	bool RouterInfo::IsNAT2NATOnly (const RouterInfo & other) const
	{
		return !(m_PublishedTransports.load () & other.m_SupportedTransports.load ())
			&& !(other.m_PublishedTransports.load () & m_SupportedTransports.load ());
	}

	bool RouterInfo::IsSSU2PeerTesting (bool v4) const
	{
		if (!(m_SupportedTransports.load () & (v4 ? eSSU2V4 : eSSU2V6))) return false;
		auto addr = (*GetAddresses ())[v4 ? eSSU2V4Idx : eSSU2V6Idx];
		return addr && addr->IsPeerTesting () && addr->IsReachableSSU ();
	}

	bool RouterInfo::IsSSU2Introducer (bool v4) const
	{
		if (!(m_SupportedTransports.load () & (v4 ? eSSU2V4 : eSSU2V6))) return false;
		auto addr = (*GetAddresses ())[v4 ? eSSU2V4Idx : eSSU2V6Idx];
		return addr && addr->IsIntroducer () && !addr->host.is_unspecified () && addr->port;
	}

	void RouterInfo::SetUnreachableAddressesTransportCaps (uint8_t transports)
	{
		// A split address object belongs to exactly one family slot. Applying
		// the caller's combined "46" mask to every object would make a v4-only
		// address claim eV6 while the v6 slot stays empty.
		auto addresses = GetAddresses ();
		if (!addresses) return;
		for (uint8_t i = 0; i < eNumTransports; i++)
		{
			auto & addr = (*addresses)[i];
			if (!addr || addr->published) continue;
			uint8_t family = 0;
			switch (i)
			{
			case eNTCP2V4Idx:
			case eSSU2V4Idx: family = eV4; break;
			case eNTCP2V6Idx:
			case eSSU2V6Idx:
			case eNTCP2V6MeshIdx: family = eV6; break;
			default: continue;
			}
			addr->caps &= ~(eV4 | eV6);
			if (transports & family) addr->caps |= family;
		}
	}

	void RouterInfo::UpdateSupportedTransports ()
	{
		auto addresses = GetAddresses ();
		if (!addresses) return;
		CompatibleTransports supported = 0, reachable = 0, published = 0;
		ComputeAddressTransports (*addresses, supported, reachable, published);
		m_SupportedTransports.store (supported);
		m_ReachableTransports.store (reachable);
		m_PublishedTransports.store (published);
	}

	void RouterInfo::UpdateIntroducers (uint64_t ts)
	{
		if (ts * 1000 < m_Timestamp.load () + INTRODUCER_UPDATE_INTERVAL) return;
		if (m_ReachableTransports.load () & eSSU2V4)
		{
			auto addr = (*GetAddresses ())[eSSU2V4Idx];
			if (addr && addr->UsesIntroducer ())
			{
				UpdateIntroducers (addr, ts);
				if (!addr->UsesIntroducer ()) // no more valid introducers
					m_ReachableTransports.fetch_and (static_cast<CompatibleTransports> (~eSSU2V4));
			}
		}
		if (m_ReachableTransports.load () & eSSU2V6)
		{
			auto addr = (*GetAddresses ())[eSSU2V6Idx];
			if (addr && addr->UsesIntroducer ())
			{
				UpdateIntroducers (addr, ts);
				if (!addr->UsesIntroducer ()) // no more valid introducers
					m_ReachableTransports.fetch_and (static_cast<CompatibleTransports> (~eSSU2V6));
			}
		}
	}

	void RouterInfo::UpdateBuffer (const uint8_t * buf, size_t len)
	{
		std::lock_guard<std::mutex> l (m_BufferMutex);
		m_IsBufferScheduledToDelete.store (false);
		if (!m_Buffer) m_Buffer = NewBuffer ();
		if (!m_Buffer || !buf) return;
		if (len > m_Buffer->size ()) len = m_Buffer->size ();
		// Update(GetBuffer(), GetBufferLen()) would otherwise call memcpy()
		// with identical source and destination.
		if (buf != m_Buffer->data ()) memcpy (m_Buffer->data (), buf, len);
		m_Buffer->SetBufferLen (len);
	}

	std::shared_ptr<RouterInfo::Buffer> RouterInfo::CopyBuffer () const
	{
		std::lock_guard<std::mutex> l (m_BufferMutex);
		if (!m_Buffer) return nullptr;
		return netdb.NewRouterInfoBuffer (*m_Buffer);
	}

	bool RouterInfo::TakeUpdatedBuffer (std::shared_ptr<Buffer> & buffer, bool & deleted)
	{
		std::lock_guard<std::mutex> l (m_BufferMutex);
		deleted = m_IsBufferScheduledToDelete.exchange (false);
		if (deleted) m_Buffer = nullptr;
		const bool updated = m_IsUpdated.exchange (false);
		if (updated && !deleted && m_Buffer) buffer = netdb.NewRouterInfoBuffer (*m_Buffer);
		return updated;
	}

	std::shared_ptr<RouterInfo::Buffer> RouterInfo::NewBuffer () const
	{
		return netdb.NewRouterInfoBuffer ();
	}

	std::shared_ptr<RouterInfo::Address> RouterInfo::NewAddress () const
	{
		return netdb.NewRouterInfoAddress ();
	}

	RouterInfo::AddressesPtr RouterInfo::NewAddresses () const
	{
		return netdb.NewRouterInfoAddresses ();
	}

	std::shared_ptr<IdentityEx> RouterInfo::NewIdentity (const uint8_t * buf, size_t len) const
	{
		return netdb.NewIdentity (buf, len);
	}

	void RouterInfo::RefreshTimestamp ()
	{
		m_Timestamp.store (i2p::util::GetMillisecondsSinceEpoch ());
	}

	bool RouterInfo::IsHighCongestion (bool highBandwidth) const
	{
		switch (m_Congestion.load ())
		{
		case eLowCongestion: return false; break;
		case eMediumCongestion: return highBandwidth; break;
		case eHighCongestion:
			return i2p::util::GetMillisecondsSinceEpoch () < m_Timestamp.load () + HIGH_CONGESTION_INTERVAL * 1000LL;
			break;
		case eRejectAll: return true; break;
		default: return false;
		}
	}

	std::string RouterInfo::GetTransportName (SupportedTransports tr)
	{
		switch (tr)
		{
		case eNTCP2V4: return "NTCP2V4";
		case eNTCP2V6: return "NTCP2V6";
		case eSSU2V4: return "SSU2V4";
		case eSSU2V6: return "SSU2V6";
		case eNTCP2V6Mesh: return "Mesh";
		default: return "";
		}
	}

	bool RouterInfo::IsSameSubnet (const RouterInfo & other) const
	{
		auto transports = m_SupportedTransports.load () & other.m_SupportedTransports.load ();
		if (!transports) return false;
		auto addresses1 = GetAddresses (), addresses2 = other.GetAddresses ();
		if (!addresses1 || !addresses2) return false;
		for (int i = 0; i < eNumTransports; i++)
			if (i != eNTCP2V6MeshIdx && (transports & (1 << i)))
			{
				auto addr1 = (*addresses1)[i], addr2 = (*addresses2)[i];
				if (addr1 && addr2 && !addr1->host.is_unspecified () && !addr2->host.is_unspecified ())
					return addr1->IsSameSubnet (*addr2); // first address with IPs
			}
		return false;
	}

	void LocalRouterInfo::CreateBuffer (const PrivateKeys & privateKeys)
	{
		auto publicKeys = privateKeys.GetPublic ();
		if (!publicKeys) return;
		const uint64_t previousTimestamp = GetTimestamp ();
		RefreshTimestamp ();
		std::stringstream s;
		uint8_t ident[1024];
		auto identLen = publicKeys->ToBuffer (ident, sizeof (ident));
		if (!identLen)
		{
			LogPrint (eLogError, "RouterInfo: Can't serialize our identity");
			SetTimestamp (previousTimestamp);
			return;
		}
		auto signatureLen = publicKeys->GetSignatureLen ();
		s.write ((char *)ident, identLen);
		WriteToStream (s);
		// Keep one copy; s.str() returns a temporary and was previously
		// materialized three times.
		const std::string body = s.str ();
		size_t len = body.size ();
		if (len + signatureLen <= MAX_RI_BUFFER_SIZE)
		{
			UpdateBuffer ((const uint8_t *)body.data (), len);
			auto buffer = GetSharedBuffer ();
			if (buffer && buffer->GetBufferLen () == len)
			{
				// signature
				// len is bounded by MAX_RI_BUFFER_SIZE above.
				privateKeys.Sign (buffer->data (), static_cast<int> (len), buffer->data () + len);
				SetBufferLen (len + signatureLen);
				return;
			}
			LogPrint (eLogError, "RouterInfo: Can't allocate buffer for our RouterInfo");
		}
		else
			LogPrint (eLogError, "RouterInfo: Our RouterInfo is too long ", len + signatureLen);
		// Do not leave m_Timestamp newer than the retained (old) buffer.
		SetTimestamp (previousTimestamp);
	}

	void LocalRouterInfo::UpdateCaps (uint8_t caps)
	{
		SetCaps (caps);
		// Keep IsFloodfill() consistent with the "f" cap (Java reads the cap
		// string each time). RouterContext keeps its own policy flag separately.
		if (caps & i2p::data::RouterInfo::eFloodfill)
			SetFloodfill ();
		else
			ResetFloodfill ();
		UpdateCapsProperty ();
	}

	void LocalRouterInfo::UpdateCapsProperty ()
	{
		std::string caps;
		uint8_t c = GetCaps ();
		if (c & eFloodfill)
		{
			if (c & eExtraBandwidth)
				caps += (c & eHighBandwidth) ? CAPS_FLAG_EXTRA_BANDWIDTH2 : // 'X'
					CAPS_FLAG_EXTRA_BANDWIDTH1; // 'P'
			else
				caps += CAPS_FLAG_HIGH_BANDWIDTH; // 'O'
			caps += CAPS_FLAG_FLOODFILL; // floodfill
		}
		else
		{
			if (c & eExtraBandwidth)
				caps += (c & eHighBandwidth) ? CAPS_FLAG_EXTRA_BANDWIDTH2 /* 'X' */
											 : CAPS_FLAG_EXTRA_BANDWIDTH1; /*'P' */
			else
				caps += (c & eHighBandwidth) ? CAPS_FLAG_HIGH_BANDWIDTH /* 'O' */ : CAPS_FLAG_LOW_BANDWIDTH2 /* 'L' */
					; // bandwidth
		}
		if (c & eHidden) caps += CAPS_FLAG_HIDDEN; // hidden
		if (c & eReachable) caps += CAPS_FLAG_REACHABLE; // reachable
		if (c & eUnreachable) caps += CAPS_FLAG_UNREACHABLE; // unreachable

		switch (GetCongestion ())
		{
		case eMediumCongestion: caps += CAPS_FLAG_MEDIUM_CONGESTION; break;
		case eHighCongestion: caps += CAPS_FLAG_HIGH_CONGESTION; break;
		case eRejectAll: caps += CAPS_FLAG_REJECT_ALL_CONGESTION; break;
		default:;
		};

		SetProperty ("caps", caps);
	}

	bool LocalRouterInfo::UpdateCongestion (Congestion c)
	{
		if (c != GetCongestion ())
		{
			SetCongestion (c);
			UpdateCapsProperty ();
			return true;
		}
		return false;
	}

	void LocalRouterInfo::WriteToStream (std::ostream & s) const
	{
		auto addresses = GetAddresses ();
		if (!addresses) return;

		uint64_t ts = htobe64 (GetTimestamp ());
		s.write ((const char *)&ts, sizeof (ts));
		// addresses
		auto v4ntcp = (*addresses)[eNTCP2V4Idx];
		auto v6ntcp = (*addresses)[eNTCP2V6Idx];
		const bool mergeNTCP2 = v4ntcp && v6ntcp && v4ntcp != v6ntcp && IsMergeableNTCP2Twin (*v4ntcp, *v6ntcp);
		uint8_t numAddresses = 0;
		for (size_t idx = 0; idx < addresses->size (); idx++)
		{
			auto addr_ptr = (*addresses)[idx];
			if (!addr_ptr) continue;
			if (idx == eNTCP2V6Idx && (addr_ptr == (*addresses)[eNTCP2V4Idx] || mergeNTCP2)) continue;
			if (idx == eSSU2V6Idx && addr_ptr == (*addresses)[eSSU2V4Idx]) continue;
			numAddresses++;
		}
		s.write ((char *)&numAddresses, sizeof (numAddresses));
		for (size_t idx = 0; idx < addresses->size (); idx++)
		{
			auto addr_ptr = (*addresses)[idx];
			if (!addr_ptr) continue;
			if (idx == eNTCP2V6Idx && (addr_ptr == (*addresses)[eNTCP2V4Idx] || mergeNTCP2)) continue;
			if (idx == eSSU2V6Idx && addr_ptr == (*addresses)[eSSU2V4Idx]) continue;
			const Address & address = *addr_ptr;
			// calculate cost
			uint8_t cost = 0x7f;
			if (address.transportStyle == eTransportNTCP2)
				cost = address.published ? COST_NTCP2_PUBLISHED : COST_NTCP2_NON_PUBLISHED;
			else if (address.transportStyle == eTransportSSU2)
				cost = address.published ? COST_SSU2_DIRECT : COST_SSU2_NON_PUBLISHED;
			else
				continue; // skip unknown address
			s.write ((const char *)&cost, sizeof (cost));
			// RouterAddress expiration is an 8-byte big-endian integer in the
			// data structure spec (Java DataHelper.writeLong(out, 8, ...)).
			// The old code wrote host order, which is only correct on big-endian
			// hosts and only harmless because local addresses use date == 0.
			uint64_t date = htobe64 (address.date);
			s.write ((const char *)&date, sizeof (date));
			std::stringstream properties;
			bool isPublished = address.published && !address.host.is_unspecified () && address.port;
			if (address.transportStyle == eTransportNTCP2)
			{
				WriteString ("NTCP2", s);
				// caps
				if (!isPublished)
				{
					WriteString ("caps", properties);
					properties << '=';
					std::string caps;
					if (address.IsV4 ()) caps += CAPS_FLAG_V4;
					if (address.IsV6 () || address.host.is_v6 ()) caps += CAPS_FLAG_V6; // we set 6 for unspecified ipv6
					// The v6 twin is not written separately; declare both
					// families on the merged v4 entry, as the pre-split writer
					// did with its shared object.
					if (mergeNTCP2 && idx == eNTCP2V4Idx) caps += CAPS_FLAG_V6;
					if (caps.empty ()) caps += CAPS_FLAG_V4;
					WriteString (caps, properties);
					properties << ';';
				}
			}
			else if (address.transportStyle == eTransportSSU2)
			{
				WriteString ("SSU2", s);
				// caps
				std::string caps;
				// "B" (peer testing) is valid for non-published SSU2 addresses
				// too: a firewalled router with introducers still acts as Bob or
				// Charlie. Java publishes CAP_TESTING_4/CAP_TESTING_6 ("B4"/"B6")
				// in this case. "C" (introducer) requires a reachable endpoint.
				if (address.IsPeerTesting ()) caps += CAPS_FLAG_SSU2_TESTING;
				if (isPublished)
				{
					if (address.IsIntroducer ()) caps += CAPS_FLAG_SSU2_INTRODUCER;
				}
				else
				{
					if (address.IsV4 ()) caps += CAPS_FLAG_V4;
					if (address.IsV6 () || address.host.is_v6 ()) caps += CAPS_FLAG_V6; // we set 6 for unspecified ipv6
					if (!address.IsV4 () && !address.IsV6 () && !address.host.is_v6 ()) caps += CAPS_FLAG_V4;
				}
				if (!caps.empty ())
				{
					WriteString ("caps", properties);
					properties << '=';
					WriteString (caps, properties);
					properties << ';';
				}
			}
			else
				WriteString ("", s);

			if (isPublished && !address.host.is_unspecified ())
			{
				WriteString ("host", properties);
				properties << '=';
				WriteString (i2p::util::net::FormatAddress (address.host), properties);
				properties << ';';
			}
			if ((address.IsNTCP2 () && isPublished) || address.IsSSU2 ())
			{
				// publish i for NTCP2 or SSU2
				WriteString ("i", properties);
				properties << '=';
				size_t len = address.IsSSU2 () ? 32 : 16;
				WriteString (address.i.ToBase64 (len), properties);
				properties << ';';
			}
			if (address.transportStyle == eTransportSSU2)
			{
				// write introducers if any
				if (address.ssu && !address.ssu->introducers.empty ())
				{
					int i = 0;
					for (const auto & introducer : address.ssu->introducers)
					{
						if (!introducer.iTag) continue;
						if (introducer.iExp) // expiration is specified
						{
							WriteString ("iexp" + std::to_string (i), properties);
							properties << '=';
							WriteString (std::to_string (introducer.iExp), properties);
							properties << ';';
						}
						i++;
					}
					i = 0;
					for (const auto & introducer : address.ssu->introducers)
					{
						if (!introducer.iTag) continue;
						WriteString ("ih" + std::to_string (i), properties);
						properties << '=';
						auto value = ByteStreamToBase64 (introducer.iH, 32);
						WriteString (value, properties);
						properties << ';';
						i++;
					}
					i = 0;
					for (const auto & introducer : address.ssu->introducers)
					{
						if (!introducer.iTag) continue;
						WriteString ("itag" + std::to_string (i), properties);
						properties << '=';
						WriteString (std::to_string (introducer.iTag), properties);
						properties << ';';
						i++;
					}
				}
			}

			if (address.IsSSU2 ())
			{
				// write mtu
				if (address.ssu && address.ssu->mtu
					&& address.ssu->mtu < (int)i2p::transport::SSU2_MAX_PACKET_SIZE) // omit if mtu = 1500
				{
					WriteString ("mtu", properties);
					properties << '=';
					WriteString (std::to_string (address.ssu->mtu), properties);
					properties << ';';
				}
			}
			if (isPublished && address.port)
			{
				WriteString ("port", properties);
				properties << '=';
				WriteString (std::to_string (address.port), properties);
				properties << ';';
			}
#if OPENSSL_PQ
			if (address.IsSSU2 () && address.v >= 3)
			{
				// SSU2 "pq" is a preference list. Java UDPTransport publishes
				// PQ_VERSION "4,3" when the interface can carry ML-KEM-768 and
				// PQ_VERSION_LOW_MTU "3" otherwise (addSSU2Options()).
				// ML-KEM-768 needs 1318 bytes on IPv4 and 1338 on IPv6
				// (PeerState2.MIN_MLKEM768_IPV4_MTU / _IPV6_MTU). Only write a
				// parameter set that the active provider actually implements.
				bool ipv6 = address.IsV6 () && !address.IsV4 ();
				int minMtu = ipv6 ? MIN_SSU2_MLKEM768_MTU_IPV6 : MIN_SSU2_MLKEM768_MTU_IPV4;
				int mtu = address.ssu ? address.ssu->mtu : 0;
				const bool allow4 = address.v >= 4 && (!mtu || mtu >= minMtu)
					&& i2p::crypto::CanPublishPQ (4);
				const bool allow3 = address.v >= 3 && i2p::crypto::CanPublishPQ (3);
				if (allow4 || allow3)
				{
					WriteString ("pq", properties);
					properties << '=';
					if (allow4) WriteString ("4", properties);
					if (allow4 && allow3) properties << ',';
					if (allow3) WriteString ("3", properties);
					properties << ';';
				}
			}
			else if (address.v >= 3) // NTCP2 post quantum
			{
				const int publishVersion = i2p::crypto::HighestSupportedPQVersion (address.v, 5);
				if (publishVersion)
				{
					WriteString ("pq", properties);
					properties << '=';
					WriteString (std::to_string (publishVersion), properties);
					properties << ';';
				}
			}
#endif
			if (address.IsNTCP2 () || address.IsSSU2 ())
			{
				// publish s and v for NTCP2 or SSU2
				WriteString ("s", properties);
				properties << '=';
				WriteString (address.s.ToBase64 (), properties);
				properties << ';';
				WriteString ("v", properties);
				properties << '=';
				WriteString ("2", properties);
				properties << ';';
			}

			uint16_t size = htobe16 (static_cast<uint16_t> (properties.str ().size ()));
			s.write ((char *)&size, sizeof (size));
			s.write (properties.str ().c_str (), properties.str ().size ());
		}

		// peers
		uint8_t numPeers = 0;
		s.write ((char *)&numPeers, sizeof (numPeers));

		// properties
		std::stringstream properties;
		for (const auto & p : m_Properties)
		{
			WriteString (p.first, properties);
			properties << '=';
			WriteString (p.second, properties);
			properties << ';';
		}
		uint16_t size = htobe16 (static_cast<uint16_t> (properties.str ().size ()));
		s.write ((char *)&size, sizeof (size));
		s.write (properties.str ().c_str (), properties.str ().size ());
	}

	void LocalRouterInfo::SetProperty (std::string_view key, std::string_view value)
	{
		auto [it, inserted] = m_Properties.emplace (key, value);
		if (!inserted) it->second = value;
	}

	void LocalRouterInfo::DeleteProperty (const std::string & key)
	{
		m_Properties.erase (key);
	}

	std::string LocalRouterInfo::GetProperty (const std::string & key) const
	{
		auto it = m_Properties.find (key);
		if (it != m_Properties.end ()) return it->second;
		return "";
	}

	void LocalRouterInfo::UpdateFloodfillProperty (bool floodfill)
	{
		if (floodfill)
		{
			UpdateCaps (GetCaps () | i2p::data::RouterInfo::eFloodfill);
			SetFloodfill ();
		}
		else
		{
			UpdateCaps (GetCaps () & ~i2p::data::RouterInfo::eFloodfill);
			ResetFloodfill ();
		}
	}

	void LocalRouterInfo::WriteString (const std::string & str, std::ostream & s) const
	{
		// Java DataHelper.writeString()/writeProperties() throw on strings over
		// 255 bytes. CreateBuffer() cannot signal an error, so keep the mapping
		// well-formed and make the loss visible instead of silently wrapping
		// the length byte (256 bytes used to become an empty value).
		if (str.size () > 255)
			LogPrint (eLogError, "RouterInfo: String of ", str.size (), " bytes exceeds the 255-byte mapping limit");
		uint8_t len = static_cast<uint8_t> (std::min<size_t> (str.size (), 255));
		s.write ((char *)&len, 1);
		s.write (str.c_str (), len);
	}

	std::shared_ptr<RouterInfo::Buffer> LocalRouterInfo::NewBuffer () const
	{
		return std::make_shared<Buffer> ();
	}

	std::shared_ptr<RouterInfo::Address> LocalRouterInfo::NewAddress () const
	{
		return std::make_shared<Address> ();
	}

	RouterInfo::AddressesPtr LocalRouterInfo::NewAddresses () const
	{
		return RouterInfo::AddressesPtr (new RouterInfo::Addresses ());
	}

	std::shared_ptr<IdentityEx> LocalRouterInfo::NewIdentity (const uint8_t * buf, size_t len) const
	{
		return std::make_shared<IdentityEx> (buf, len);
	}

	bool LocalRouterInfo::AddSSU2Introducer (const Introducer & introducer, bool v4)
	{
		if (!introducer.iTag) return false; // Java requires tag > 0
		auto addresses = GetAddresses ();
		if (!addresses) return false;
		auto addr = (*addresses)[v4 ? eSSU2V4Idx : eSSU2V6Idx];
		if (!addr || !addr->ssu) return false;
		for (auto & intro : addr->ssu->introducers)
			if (intro.iTag == introducer.iTag) return false; // already presented
		// SSU2 RouterAddress mappings only define itag0..2/ih0..2/iexp0..2.
		if (addr->ssu->introducers.size () >= MAX_RI_INTRODUCERS)
		{
			LogPrint (eLogWarning, "RouterInfo: Too many SSU2 introducers, not added");
			return false;
		}
		addr->ssu->introducers.push_back (introducer);
		if (!addr->ssu->introducers.back ().iExp)
			addr->ssu->introducers.back ().iExp = std::numeric_limits<uint32_t>::max ();
		SetReachableTransports (
			static_cast<CompatibleTransports> (GetReachableTransports () | (addr->IsV4 () ? eSSU2V4 : eSSU2V6)));
		return true;
	}

	bool LocalRouterInfo::RemoveSSU2Introducer (const IdentHash & h, bool v4)
	{
		auto addresses = GetAddresses ();
		if (!addresses) return false;
		auto addr = (*addresses)[v4 ? eSSU2V4Idx : eSSU2V6Idx];
		if (addr && addr->ssu)
		{
			for (auto it = addr->ssu->introducers.begin (); it != addr->ssu->introducers.end (); ++it)
				if (h == it->iH)
				{
					addr->ssu->introducers.erase (it);
					if (addr->ssu->introducers.empty ())
						SetReachableTransports (
							GetReachableTransports () & (CompatibleTransports) ~(addr->IsV4 () ? eSSU2V4 : eSSU2V6));
					return true;
				}
		}
		return false;
	}

	bool LocalRouterInfo::UpdateSSU2Introducer (const IdentHash & h, bool v4, uint32_t iTag, uint32_t iExp)
	{
		auto addresses = GetAddresses ();
		if (!addresses) return false;
		auto addr = (*addresses)[v4 ? eSSU2V4Idx : eSSU2V6Idx];
		if (addr && addr->ssu)
		{
			for (auto & it : addr->ssu->introducers)
				if (h == it.iH)
				{
					it.iTag = iTag;
					it.iExp = iExp;
					return true;
				}
		}
		return false;
	}
}
}
