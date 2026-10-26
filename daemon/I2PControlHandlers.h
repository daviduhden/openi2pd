/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PControlHandlers.
 *
 * Declares the I2PControl method handlers that expose router status, tunnel,
 * transport, NetDb and client-destination data and apply bandwidth settings.
 * Handlers append JSON fields through type-specific InsertParam overloads.
 * The header also owns the header-only JSON-RPC envelope and parameter
 * helpers, so escaping, request-id typing and bandwidth validation have one
 * implementation shared by the service and its compile-free regression.
 *
 */

#ifndef I2P_CONTROL_HANDLERS_H__
#define I2P_CONTROL_HANDLERS_H__

#include <charconv>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <boost/property_tree/ptree.hpp>

namespace i2p
{
namespace client
{
	/*
	 * JSON-RPC 2.0 error codes.
	 *
	 * -32001..-32006 are the documented I2PControl values and are reused
	 * verbatim so an existing Java or Perl client sees the same failures.
	 * -32007 is this implementation's authentication throttle; a failed
	 * CSPRNG uses the standard INTERNAL_ERROR.
	 */
	constexpr int I2P_CONTROL_ERROR_PARSE = -32700;
	constexpr int I2P_CONTROL_ERROR_INVALID_REQUEST = -32600;
	constexpr int I2P_CONTROL_ERROR_METHOD_NOT_FOUND = -32601;
	constexpr int I2P_CONTROL_ERROR_INVALID_PARAMS = -32602;
	constexpr int I2P_CONTROL_ERROR_INTERNAL = -32603;
	constexpr int I2P_CONTROL_ERROR_INVALID_PASSWORD = -32001;
	constexpr int I2P_CONTROL_ERROR_NO_TOKEN = -32002;
	constexpr int I2P_CONTROL_ERROR_UNKNOWN_TOKEN = -32003;
	constexpr int I2P_CONTROL_ERROR_EXPIRED_TOKEN = -32004;
	constexpr int I2P_CONTROL_ERROR_API_UNSPECIFIED = -32005;
	constexpr int I2P_CONTROL_ERROR_API_UNSUPPORTED = -32006;
	constexpr int I2P_CONTROL_ERROR_RATE_LIMITED = -32007;

	// The only API version this service implements, matching Java 2.14.0.
	constexpr unsigned I2P_CONTROL_SUPPORTED_API = 1;

	namespace json
	{
		/*
		 * Kind of the request id. JSON-RPC allows a string, a number or null;
		 * property_tree discards the distinction, so the bounded request body
		 * is scanned separately to keep an integer id an integer and to escape
		 * a string id instead of emitting it verbatim.
		 */
		enum class JSONIdKind : uint8_t
		{
			Missing = 0, // absent, or the request is not a JSON object
			Integer,
			Float, // any other number literal; rejected by the dispatcher
			String,
			Boolean,
			Null,
			Structured, // object or array
			Invalid // malformed value
		};

		struct JSONRequestId
		{
			JSONIdKind kind = JSONIdKind::Missing;
			// Decoded string value, or the verbatim literal for the other kinds.
			std::string value;
		};

		/*
		 * Validates and copies one UTF-8 sequence. Rejects a lone continuation
		 * byte, an overlong form, a UTF-16 surrogate and a value above U+10FFFF.
		 */
		inline bool AppendUTF8Sequence (std::string & output, std::string_view value, size_t & index)
		{
			const auto byte = static_cast<unsigned char> (value[index]);
			if (byte < 0x80)
			{
				output.push_back (static_cast<char> (byte));
				++index;
				return true;
			}
			size_t length = 0;
			uint32_t codePoint = 0;
			if ((byte & 0xE0) == 0xC0)
			{
				length = 2;
				codePoint = byte & 0x1F;
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				length = 3;
				codePoint = byte & 0x0F;
			}
			else if ((byte & 0xF8) == 0xF0)
			{
				length = 4;
				codePoint = byte & 0x07;
			}
			else
				return false;
			if (index + length > value.size ()) return false;
			for (size_t i = 1; i < length; ++i)
			{
				const auto continuation = static_cast<unsigned char> (value[index + i]);
				if ((continuation & 0xC0) != 0x80) return false;
				codePoint = (codePoint << 6) | (continuation & 0x3F);
			}
			if ((length == 2 && codePoint < 0x80) || (length == 3 && codePoint < 0x800)
				|| (length == 4 && codePoint < 0x10000) || codePoint > 0x10FFFF
				|| (codePoint >= 0xD800 && codePoint <= 0xDFFF))
				return false;
			output.append (value.substr (index, length));
			index += length;
			return true;
		}

		/*
		 * Appends one quoted, escaped JSON string.
		 *
		 * Returns false for invalid UTF-8 (a lone continuation byte, an
		 * overlong form, a UTF-16 surrogate or a value above U+10FFFF) without
		 * modifying `output`, so a caller never publishes malformed JSON.
		 */
		inline bool AppendQuoted (std::string & output, std::string_view value)
		{
			std::string escaped;
			escaped.reserve (value.size () + 2);
			escaped.push_back ('"');
			size_t index = 0;
			while (index < value.size ())
			{
				const auto byte = static_cast<unsigned char> (value[index]);
				switch (byte)
				{
				case '"':
					escaped.append ("\\\"");
					++index;
					continue;
				case '\\':
					escaped.append ("\\\\");
					++index;
					continue;
				case '\b':
					escaped.append ("\\b");
					++index;
					continue;
				case '\t':
					escaped.append ("\\t");
					++index;
					continue;
				case '\n':
					escaped.append ("\\n");
					++index;
					continue;
				case '\f':
					escaped.append ("\\f");
					++index;
					continue;
				case '\r':
					escaped.append ("\\r");
					++index;
					continue;
				default: break;
				}
				if (byte < 0x20)
				{
					static constexpr char hex[] = "0123456789abcdef";
					escaped.append ("\\u00");
					escaped.push_back (hex[byte >> 4]);
					escaped.push_back (hex[byte & 0x0F]);
					++index;
					continue;
				}
				if (!AppendUTF8Sequence (escaped, value, index)) return false;
			}
			escaped.push_back ('"');
			output.append (escaped);
			return true;
		}

		// Quoted string, or the JSON literal null when the bytes are not UTF-8.
		inline std::string Quote (std::string_view value)
		{
			std::string result;
			if (!AppendQuoted (result, value)) return "null";
			return result;
		}

		namespace detail
		{
			inline void SkipWhitespace (std::string_view document, size_t & index)
			{
				while (index < document.size ()
					&& (document[index] == ' ' || document[index] == '\t' || document[index] == '\n'
						|| document[index] == '\r'))
					++index;
			}

			inline int HexDigit (char value)
			{
				if (value >= '0' && value <= '9') return value - '0';
				if (value >= 'a' && value <= 'f') return value - 'a' + 10;
				if (value >= 'A' && value <= 'F') return value - 'A' + 10;
				return -1;
			}

			inline void AppendCodePoint (std::string & output, uint32_t codePoint)
			{
				if (codePoint < 0x80)
					output.push_back (static_cast<char> (codePoint));
				else if (codePoint < 0x800)
				{
					output.push_back (static_cast<char> (0xC0 | (codePoint >> 6)));
					output.push_back (static_cast<char> (0x80 | (codePoint & 0x3F)));
				}
				else if (codePoint < 0x10000)
				{
					output.push_back (static_cast<char> (0xE0 | (codePoint >> 12)));
					output.push_back (static_cast<char> (0x80 | ((codePoint >> 6) & 0x3F)));
					output.push_back (static_cast<char> (0x80 | (codePoint & 0x3F)));
				}
				else
				{
					output.push_back (static_cast<char> (0xF0 | (codePoint >> 18)));
					output.push_back (static_cast<char> (0x80 | ((codePoint >> 12) & 0x3F)));
					output.push_back (static_cast<char> (0x80 | ((codePoint >> 6) & 0x3F)));
					output.push_back (static_cast<char> (0x80 | (codePoint & 0x3F)));
				}
			}

			// Decodes one JSON string token (including its quotes). Rejects raw
			// control bytes, invalid escapes and lone UTF-16 surrogates.
			inline bool ParseString (std::string_view document, size_t & index, std::string & output)
			{
				if (index >= document.size () || document[index] != '"') return false;
				++index;
				while (index < document.size ())
				{
					const auto byte = static_cast<unsigned char> (document[index]);
					if (byte == '"')
					{
						++index;
						return true;
					}
					if (byte < 0x20) return false;
					if (byte != '\\')
					{
						if (!AppendUTF8Sequence (output, document, index)) return false;
						continue;
					}
					++index;
					if (index >= document.size ()) return false;
					const char escape = document[index++];
					switch (escape)
					{
					case '"': output.push_back ('"'); break;
					case '\\': output.push_back ('\\'); break;
					case '/': output.push_back ('/'); break;
					case 'b': output.push_back ('\b'); break;
					case 'f': output.push_back ('\f'); break;
					case 'n': output.push_back ('\n'); break;
					case 'r': output.push_back ('\r'); break;
					case 't': output.push_back ('\t'); break;
					case 'u':
					{
						if (index + 4 > document.size ()) return false;
						uint32_t codePoint = 0;
						for (int i = 0; i < 4; ++i)
						{
							const int digit = HexDigit (document[index + i]);
							if (digit < 0) return false;
							codePoint = (codePoint << 4) | static_cast<uint32_t> (digit);
						}
						index += 4;
						if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
						{
							// A high surrogate must be followed by a low one.
							if (index + 6 > document.size () || document[index] != '\\' || document[index + 1] != 'u')
								return false;
							uint32_t low = 0;
							for (int i = 0; i < 4; ++i)
							{
								const int digit = HexDigit (document[index + 2 + i]);
								if (digit < 0) return false;
								low = (low << 4) | static_cast<uint32_t> (digit);
							}
							if (low < 0xDC00 || low > 0xDFFF) return false;
							index += 6;
							codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
						}
						else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
							return false;
						AppendCodePoint (output, codePoint);
						break;
					}
					default: return false;
					}
				}
				return false;
			}

			// Skips one value of any kind. Iterative with an explicit bound so
			// a hostile nesting depth cannot exhaust the control thread stack.
			inline bool SkipValue (std::string_view document, size_t & index)
			{
				constexpr size_t MAX_DEPTH = 32;
				size_t depth = 0;
				bool inString = false;
				bool escaped = false;
				while (index < document.size ())
				{
					const char value = document[index];
					if (inString)
					{
						++index;
						if (escaped)
							escaped = false;
						else if (value == '\\')
							escaped = true;
						else if (value == '"')
							inString = false;
						continue;
					}
					switch (value)
					{
					case '"':
						inString = true;
						++index;
						break;
					case '{':
					case '[':
						if (++depth > MAX_DEPTH) return false;
						++index;
						break;
					case '}':
					case ']':
						if (depth == 0) return true; // end of the enclosing object
						--depth;
						++index;
						if (depth == 0) return true;
						break;
					case ',':
						if (depth == 0) return true;
						++index;
						break;
					default: ++index; break;
					}
				}
				return false;
			}
		}

		/*
		 * Locates the top-level "id" member of a bounded JSON-RPC request.
		 * The request body has already been accepted by the property_tree
		 * parser; this scan only recovers the member's JSON type.
		 */
		inline JSONRequestId ParseRequestId (std::string_view document)
		{
			JSONRequestId result;
			size_t index = 0;
			detail::SkipWhitespace (document, index);
			if (index >= document.size () || document[index] != '{') return result;
			++index;
			bool afterComma = false;
			for (;;)
			{
				detail::SkipWhitespace (document, index);
				if (index >= document.size ()) return { JSONIdKind::Invalid, { } };
				if (document[index] == '}')
				{
					// A trailing comma is not valid JSON.
					if (afterComma) return { JSONIdKind::Invalid, { } };
					++index;
					return result;
				}
				std::string name;
				if (!detail::ParseString (document, index, name)) return { JSONIdKind::Invalid, { } };
				detail::SkipWhitespace (document, index);
				if (index >= document.size () || document[index] != ':') return { JSONIdKind::Invalid, { } };
				++index;
				detail::SkipWhitespace (document, index);
				if (index >= document.size ()) return { JSONIdKind::Invalid, { } };

				JSONIdKind kind = JSONIdKind::Invalid;
				std::string value;
				const char first = document[index];
				if (first == '"')
				{
					kind = JSONIdKind::String;
					if (!detail::ParseString (document, index, value)) return { JSONIdKind::Invalid, { } };
				}
				else if (first == '{' || first == '[')
				{
					kind = JSONIdKind::Structured;
					if (!detail::SkipValue (document, index)) return { JSONIdKind::Invalid, { } };
				}
				else if (first == 't' || first == 'f' || first == 'n')
				{
					const auto start = index;
					while (index < document.size () && document[index] >= 'a' && document[index] <= 'z') ++index;
					value.assign (document.substr (start, index - start));
					if (value == "true" || value == "false")
						kind = JSONIdKind::Boolean;
					else if (value == "null")
						kind = JSONIdKind::Null;
					else
						return { JSONIdKind::Invalid, { } };
				}
				else if (first == '-' || (first >= '0' && first <= '9'))
				{
					const auto start = index;
					bool integral = true;
					while (index < document.size ())
					{
						const char digit = document[index];
						if (digit == '.' || digit == 'e' || digit == 'E')
							integral = false;
						else if (digit != '-' && digit != '+' && (digit < '0' || digit > '9'))
							break;
						++index;
					}
					if (index == start) return { JSONIdKind::Invalid, { } };
					value.assign (document.substr (start, index - start));
					kind = integral ? JSONIdKind::Integer : JSONIdKind::Float;
				}
				else
					return { JSONIdKind::Invalid, { } };

				if (name == "id")
				{
					result.kind = kind;
					result.value = std::move (value);
				}

				detail::SkipWhitespace (document, index);
				if (index >= document.size ()) return { JSONIdKind::Invalid, { } };
				if (document[index] == ',')
				{
					++index;
					afterComma = true;
					continue;
				}
				if (document[index] == '}')
				{
					++index;
					return result;
				}
				return { JSONIdKind::Invalid, { } };
			}
		}
	}

	// Strict unsigned decimal parser: no sign, no whitespace, no trailing bytes.
	inline bool ParseUnsignedDecimal (std::string_view text, unsigned & value)
	{
		value = 0;
		if (text.empty () || text.size () > 10) return false;
		unsigned parsed = 0;
		const auto * const first = text.data ();
		const auto * const last = first + text.size ();
		const auto result = std::from_chars (first, last, parsed);
		if (result.ec != std::errc () || result.ptr != last) return false;
		value = parsed;
		return true;
	}

	/*
	 * Bandwidth-setting operands are in KiB/s, the unit RouterContext stores.
	 * Zero is accepted and leaves the router default in force; the upper bound
	 * is the value RouterContext itself uses for "unlimited" ('X', 1 GiB/s).
	 */
	constexpr unsigned I2P_CONTROL_MAX_BANDWIDTH_LIMIT = 1000000;

	enum class BandwidthLimitResult : uint8_t
	{
		Ok = 0,
		Invalid,
		OutOfRange
	};

	inline BandwidthLimitResult ParseBandwidthLimit (std::string_view value, unsigned & limit)
	{
		limit = 0;
		if (value.empty ()) return BandwidthLimitResult::Invalid;
		// Anything this long is beyond the accepted range: reject before the
		// conversion so a hostile operand cannot drive a long digit scan.
		if (value.size () > 16) return BandwidthLimitResult::OutOfRange;
		unsigned parsed = 0;
		const auto * const first = value.data ();
		const auto * const last = first + value.size ();
		const auto result = std::from_chars (first, last, parsed);
		if (result.ec == std::errc::result_out_of_range) return BandwidthLimitResult::OutOfRange;
		if (result.ec != std::errc () || result.ptr != last) return BandwidthLimitResult::Invalid;
		if (parsed > I2P_CONTROL_MAX_BANDWIDTH_LIMIT) return BandwidthLimitResult::OutOfRange;
		limit = parsed;
		return BandwidthLimitResult::Ok;
	}

	class I2PControlHandlers
	{
	public:
		I2PControlHandlers ();

		// methods
		// TODO: make protected
		void RouterInfoHandler (const boost::property_tree::ptree & params, std::ostringstream & results);
		void NetworkSettingHandler (const boost::property_tree::ptree & params, std::ostringstream & results);
		void ClientServicesInfoHandler (const boost::property_tree::ptree & params, std::ostringstream & results);
		void LocalDestinationInfoHandler (const boost::property_tree::ptree & params, std::ostringstream & results);

	protected:
		void InsertParam (std::ostringstream & ss, const std::string & name, int value) const;
		void InsertParam (std::ostringstream & ss, const std::string & name, double value) const;
		void InsertParam (
			std::ostringstream & ss, const std::string & name, const std::string & value, bool quotes = true) const;
		void InsertParam (
			std::ostringstream & ss, const std::string & name, const boost::property_tree::ptree & value) const;

	private:
		// RouterInfo
		typedef void (I2PControlHandlers::*RouterInfoRequestHandler) (std::ostringstream & results);
		void UptimeHandler (std::ostringstream & results);
		void VersionHandler (std::ostringstream & results);
		void StatusHandler (std::ostringstream & results);
		void NetDbKnownPeersHandler (std::ostringstream & results);
		void NetDbActivePeersHandler (std::ostringstream & results);
		void NetDbFloodfillsHandler (std::ostringstream & results);
		void NetDbLeaseSetsHandler (std::ostringstream & results);
		void NetStatusHandler (std::ostringstream & results);
		void NetStatusV6Handler (std::ostringstream & results);
		void NetErrorHandler (std::ostringstream & results);
		void NetErrorV6Handler (std::ostringstream & results);
		void NetTestingHandler (std::ostringstream & results);
		void NetTestingV6Handler (std::ostringstream & results);
		void TunnelsParticipatingHandler (std::ostringstream & results);
		void TunnelsInboundHandler (std::ostringstream & results);
		void TunnelsOutboundHandler (std::ostringstream & results);
		void TunnelsSuccessRateHandler (std::ostringstream & results);
		void TunnelsTotalSuccessRateHandler (std::ostringstream & results);
		void TunnelsQueueHandler (std::ostringstream & results);
		void TunnelsTBMQueueHandler (std::ostringstream & results);
		void InboundBandwidth1S (std::ostringstream & results);
		void InboundBandwidth15S (std::ostringstream & results);
		void OutboundBandwidth1S (std::ostringstream & results);
		void OutboundBandwidth15S (std::ostringstream & results);
		void TransitBandwidth15S (std::ostringstream & results);
		void NetTotalReceivedBytes (std::ostringstream & results);
		void NetTotalSentBytes (std::ostringstream & results);
		void NetTotalTransitBytes (std::ostringstream & results);

		// NetworkSetting
		typedef void (I2PControlHandlers::*NetworkSettingRequestHandler) (
			const std::string & value, std::ostringstream & results);
		void InboundBandwidthLimit (const std::string & value, std::ostringstream & results);
		void OutboundBandwidthLimit (const std::string & value, std::ostringstream & results);

		// ClientServicesInfo
		typedef void (I2PControlHandlers::*ClientServicesInfoRequestHandler) (std::ostringstream & results);
		void I2PTunnelInfoHandler (std::ostringstream & results);
		void HTTPProxyInfoHandler (std::ostringstream & results);
		void SOCKSInfoHandler (std::ostringstream & results);
		void SAMInfoHandler (std::ostringstream & results);
		void BOBInfoHandler (std::ostringstream & results);
		void I2CPInfoHandler (std::ostringstream & results);

	private:
		std::map<std::string, RouterInfoRequestHandler> m_RouterInfoHandlers;
		std::map<std::string, NetworkSettingRequestHandler> m_NetworkSettingHandlers;
		std::map<std::string, ClientServicesInfoRequestHandler> m_ClientServicesInfoHandlers;
	};
}
}

#endif
