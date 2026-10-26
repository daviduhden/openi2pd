/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i2pcontrol json.
 *
 * Compile-free regression for the I2PControl request envelope: request-id
 * typing, string escaping (including a SAM nickname that tries to inject a
 * member), the documented JSON-RPC error codes and the bandwidth operand
 * parser. No socket, certificate, router or daemon is created.
 *
 */

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include <boost/property_tree/json_parser.hpp>

#include "../daemon/I2PControlHandlers.h"

namespace
{
	int failures = 0;

	void Check (bool condition, const std::string & what)
	{
		if (condition) return;
		std::cerr << "FAIL: " << what << std::endl;
		++failures;
	}

	namespace json = i2p::client::json;
	using i2p::client::BandwidthLimitResult;
	using i2p::client::I2P_CONTROL_ERROR_API_UNSPECIFIED;
	using i2p::client::I2P_CONTROL_ERROR_API_UNSUPPORTED;
	using i2p::client::I2P_CONTROL_ERROR_EXPIRED_TOKEN;
	using i2p::client::I2P_CONTROL_ERROR_INTERNAL;
	using i2p::client::I2P_CONTROL_ERROR_INVALID_PARAMS;
	using i2p::client::I2P_CONTROL_ERROR_INVALID_PASSWORD;
	using i2p::client::I2P_CONTROL_ERROR_INVALID_REQUEST;
	using i2p::client::I2P_CONTROL_ERROR_METHOD_NOT_FOUND;
	using i2p::client::I2P_CONTROL_ERROR_NO_TOKEN;
	using i2p::client::I2P_CONTROL_ERROR_PARSE;
	using i2p::client::I2P_CONTROL_ERROR_RATE_LIMITED;
	using i2p::client::I2P_CONTROL_ERROR_UNKNOWN_TOKEN;
	using i2p::client::I2P_CONTROL_MAX_BANDWIDTH_LIMIT;
	using i2p::client::I2P_CONTROL_SUPPORTED_API;
	using i2p::client::ParseBandwidthLimit;
	using i2p::client::ParseUnsignedDecimal;

	// The reply is only accepted when it is real JSON.
	bool Parses (const std::string & document, boost::property_tree::ptree * result = nullptr)
	{
		std::istringstream stream (document);
		boost::property_tree::ptree tree;
		try
		{
			boost::property_tree::read_json (stream, tree);
		}
		catch (const std::exception &)
		{
			return false;
		}
		if (result) *result = tree;
		return true;
	}

	void CheckIdKind (std::string_view document, json::JSONIdKind expected, const std::string & what)
	{
		const auto id = json::ParseRequestId (document);
		Check (id.kind == expected, what);
	}

	void CheckBandwidth (std::string_view operand, BandwidthLimitResult expected, const std::string & what)
	{
		unsigned limit = 12345;
		const auto result = ParseBandwidthLimit (operand, limit);
		Check (result == expected, what);
		if (result == BandwidthLimitResult::Ok)
			Check (limit <= I2P_CONTROL_MAX_BANDWIDTH_LIMIT, what + " (in range)");
	}

	// Builds the same error object the dispatcher emits.
	std::string BuildError (const std::string & id, int code, std::string_view message)
	{
		std::string out = "{\"id\":";
		out += id;
		out += ",\"error\":{\"code\":";
		out += std::to_string (code);
		out += ",\"message\":";
		out += json::Quote (message);
		out += "},\"jsonrpc\":\"2.0\"}";
		return out;
	}
}

int main ()
{
	// --- request id typing -------------------------------------------------
	{
		auto id = json::ParseRequestId (R"({"id":1,"method":"Echo","params":{}})");
		Check (id.kind == json::JSONIdKind::Integer, "integer id is an integer");
		Check (id.value == "1", "integer id keeps its literal");

		id = json::ParseRequestId ("{ \"method\" : \"Echo\" ,\n \"id\" : 42 }");
		Check (id.kind == json::JSONIdKind::Integer && id.value == "42", "integer id after other members");

		id = json::ParseRequestId (R"({"id":"abc"})");
		Check (id.kind == json::JSONIdKind::String && id.value == "abc", "string id");

		id = json::ParseRequestId (R"({"id":""})");
		Check (id.kind == json::JSONIdKind::String && id.value.empty (), "empty string id");

		// The hostile id must be decoded, escaped and re-emitted as one string.
		id = json::ParseRequestId (R"({"id":"\",\"x\":1","method":"Echo"})");
		Check (id.kind == json::JSONIdKind::String, "hostile id is a string");
		Check (id.value == "\",\"x\":1", "hostile id is decoded, not split");
		{
			const auto quoted = json::Quote (id.value);
			const std::string document = "{\"id\":" + quoted + ",\"result\":{},\"jsonrpc\":\"2.0\"}";
			boost::property_tree::ptree parsed;
			Check (Parses (document, &parsed), "hostile string id produces valid JSON");
			Check (parsed.get_child ("id").data () == "\",\"x\":1", "hostile string id round-trips");
			Check (!parsed.get_child_optional ("x"), "hostile string id injects no member");
		}

		id = json::ParseRequestId (R"({"id":"\u00e9"})");
		Check (id.kind == json::JSONIdKind::String && id.value == "\xc3\xa9", "unicode escape id");

		id = json::ParseRequestId (R"({"id":"\ud83d\ude00"})");
		Check (id.kind == json::JSONIdKind::String && id.value == "\xf0\x9f\x98\x80", "surrogate pair id");

		CheckIdKind (R"({"id":true})", json::JSONIdKind::Boolean, "boolean id");
		CheckIdKind (R"({"id":false})", json::JSONIdKind::Boolean, "false id");
		CheckIdKind (R"({"id":null})", json::JSONIdKind::Null, "null id");
		CheckIdKind (R"({"id":1.5})", json::JSONIdKind::Float, "float id");
		CheckIdKind (R"({"id":1e3})", json::JSONIdKind::Float, "exponent id");
		CheckIdKind (R"({"id":[1,2]})", json::JSONIdKind::Structured, "array id");
		CheckIdKind (R"({"id":{"a":1}})", json::JSONIdKind::Structured, "object id");
		CheckIdKind (R"({"method":"Echo"})", json::JSONIdKind::Missing, "missing id");
		CheckIdKind (R"([1,2,3])", json::JSONIdKind::Missing, "non-object request");
		CheckIdKind (R"({"id":})", json::JSONIdKind::Invalid, "malformed id value");
		CheckIdKind (R"({"id":1,})", json::JSONIdKind::Invalid, "trailing comma request");
	}

	// --- string escaping ---------------------------------------------------
	{
		std::string quoted;
		Check (json::AppendQuoted (quoted, std::string_view ("a\"b\\c\nd\te")), "escaping valid UTF-8");
		Check (quoted == "\"a\\\"b\\\\c\\nd\\te\"", "quote, backslash, newline and tab are escaped");
		Check (Parses ("{\"v\":" + quoted + "}"), "escaped string is valid JSON");

		quoted.clear ();
		Check (json::AppendQuoted (quoted, std::string_view ("\x01\x1f", 2)), "escaping control bytes");
		Check (quoted == "\"\\u0001\\u001f\"", "control bytes use \\u00XX");

		quoted.clear ();
		Check (json::AppendQuoted (quoted, std::string_view ("caf\xc3\xa9", 5)), "escaping multi-byte UTF-8");
		Check (quoted == "\"caf\xc3\xa9\"", "valid UTF-8 passes through");

		// Invalid UTF-8 must never produce malformed JSON.
		Check (!json::AppendQuoted (quoted, std::string_view ("\xff", 1)), "lone 0xff rejected");
		Check (!json::AppendQuoted (quoted, std::string_view ("\xed\xa0\x80", 3)), "UTF-16 surrogate bytes rejected");
		Check (!json::AppendQuoted (quoted, std::string_view ("\xc0\xaf", 2)), "overlong form rejected");
		Check (json::Quote (std::string_view ("\xff", 1)) == "null", "invalid UTF-8 becomes null");

		// A SAM nickname is caller-supplied and is inserted as a value.
		const std::string nickname = "nick\",\"x\":1";
		const std::string document = "{\"name\":" + json::Quote (nickname) + "}";
		boost::property_tree::ptree parsed;
		Check (Parses (document, &parsed), "SAM nickname produces valid JSON");
		Check (parsed.get<std::string> ("name") == nickname, "SAM nickname round-trips");
		Check (!parsed.get_child_optional ("x"), "SAM nickname cannot inject a member");
	}

	// --- documented error codes -------------------------------------------
	{
		// Values from the I2PControl API documentation; a client matches them.
		Check (I2P_CONTROL_ERROR_PARSE == -32700, "parse error code");
		Check (I2P_CONTROL_ERROR_INVALID_REQUEST == -32600, "invalid request code");
		Check (I2P_CONTROL_ERROR_METHOD_NOT_FOUND == -32601, "method not found code");
		Check (I2P_CONTROL_ERROR_INVALID_PARAMS == -32602, "invalid params code");
		Check (I2P_CONTROL_ERROR_INTERNAL == -32603, "internal error code");
		Check (I2P_CONTROL_ERROR_INVALID_PASSWORD == -32001, "invalid password code");
		Check (I2P_CONTROL_ERROR_NO_TOKEN == -32002, "missing token code");
		Check (I2P_CONTROL_ERROR_UNKNOWN_TOKEN == -32003, "unknown token code");
		Check (I2P_CONTROL_ERROR_EXPIRED_TOKEN == -32004, "expired token code");
		Check (I2P_CONTROL_ERROR_API_UNSPECIFIED == -32005, "unspecified API code");
		Check (I2P_CONTROL_ERROR_API_UNSUPPORTED == -32006, "unsupported API code");
		Check (I2P_CONTROL_ERROR_RATE_LIMITED == -32007, "rate limited code");
		Check (I2P_CONTROL_SUPPORTED_API == 1, "supported API version");

		for (const auto code :
			{ I2P_CONTROL_ERROR_PARSE, I2P_CONTROL_ERROR_INVALID_REQUEST, I2P_CONTROL_ERROR_METHOD_NOT_FOUND,
				I2P_CONTROL_ERROR_INVALID_PARAMS, I2P_CONTROL_ERROR_INTERNAL, I2P_CONTROL_ERROR_INVALID_PASSWORD,
				I2P_CONTROL_ERROR_NO_TOKEN, I2P_CONTROL_ERROR_RATE_LIMITED })
		{
			boost::property_tree::ptree parsed;
			Check (Parses (BuildError ("1", code, "quote \" inside"), &parsed), "error object is valid JSON");
			Check (parsed.get<int> ("error.code") == code, "error object carries the code");
			Check (parsed.get<std::string> ("error.message") == "quote \" inside", "error message round-trips");
			Check (!parsed.get_child_optional ("result"), "error object has no result");
		}
	}

	// --- API / token operand parsing ---------------------------------------
	{
		unsigned value = 0;
		Check (ParseUnsignedDecimal ("1", value) && value == 1, "API 1 parses");
		Check (!ParseUnsignedDecimal ("", value), "empty API rejected");
		Check (!ParseUnsignedDecimal ("-1", value), "negative API rejected");
		Check (!ParseUnsignedDecimal ("1x", value), "trailing API bytes rejected");
		Check (!ParseUnsignedDecimal (" 1", value), "leading space rejected");
	}

	// --- bandwidth operand matrix ------------------------------------------
	{
		CheckBandwidth ("", BandwidthLimitResult::Invalid, "empty bandwidth rejected");
		CheckBandwidth ("-1", BandwidthLimitResult::Invalid, "negative bandwidth rejected");
		CheckBandwidth ("abc", BandwidthLimitResult::Invalid, "non-numeric bandwidth rejected");
		CheckBandwidth ("1x", BandwidthLimitResult::Invalid, "trailing bytes rejected");
		CheckBandwidth (" 5", BandwidthLimitResult::Invalid, "leading space rejected");
		CheckBandwidth ("5 ", BandwidthLimitResult::Invalid, "trailing space rejected");
		CheckBandwidth ("999999999999999999999", BandwidthLimitResult::OutOfRange, "overflow rejected");
		CheckBandwidth ("1000001", BandwidthLimitResult::OutOfRange, "above the maximum rejected");
		CheckBandwidth ("0", BandwidthLimitResult::Ok, "zero accepted as router default");
		CheckBandwidth ("48", BandwidthLimitResult::Ok, "low bandwidth accepted");
		CheckBandwidth ("256", BandwidthLimitResult::Ok, "high bandwidth accepted");
		CheckBandwidth ("1000000", BandwidthLimitResult::Ok, "maximum accepted");

		unsigned limit = 0;
		Check (ParseBandwidthLimit ("0", limit) == BandwidthLimitResult::Ok && limit == 0, "zero parses to zero");
		Check (ParseBandwidthLimit ("1000000", limit) == BandwidthLimitResult::Ok && limit == 1000000,
			"maximum parses exactly");
	}

	if (failures)
	{
		std::cerr << failures << " I2PControl JSON checks failed" << std::endl;
		return 1;
	}
	std::cout << "I2PControl JSON checks passed" << std::endl;
	return 0;
}
