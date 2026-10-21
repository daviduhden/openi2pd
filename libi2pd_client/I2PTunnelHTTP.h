/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: bounded incremental HTTP tunnel header transformation.
 *
 * Owns partial lines and transformed output independently of socket buffers.
 * Completed output stays stable until the parser is destroyed; callers may
 * borrow it for an asynchronous socket write without retaining input views.
 */
#ifndef I2PTUNNEL_HTTP_H__
#define I2PTUNNEL_HTTP_H__

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace i2p::client
{
inline constexpr size_t I2P_TUNNEL_HTTP_MAX_HEADER_SIZE = 8192;
inline constexpr size_t I2P_TUNNEL_MAX_NUM_HTTP_HEADERS = 100;

class I2PTunnelHTTPHeader
{
public:
	enum class Mode { ClientRequest, ServerRequest, ServerResponse };
	enum class Result { Incomplete, Complete, Rejected };

	explicit I2PTunnelHTTPHeader (Mode mode, std::string host = {}, std::string identity = {})
		: m_Mode (mode), m_Host (std::move (host)), m_Identity (std::move (identity)) { }

	Result Feed (std::string_view input)
	{
		if (m_Result != Result::Incomplete) return m_Result;
		if (m_Host.size () > I2P_TUNNEL_HTTP_MAX_HEADER_SIZE
			|| m_Identity.size () > I2P_TUNNEL_HTTP_MAX_HEADER_SIZE) return Reject ();
		for (size_t i = 0; i < input.size (); ++i)
		{
			if (++m_Bytes > I2P_TUNNEL_HTTP_MAX_HEADER_SIZE) return Reject ();
			const char ch = input[i];
			if (ch != '\n')
			{
				m_Line += ch;
				continue;
			}
			if (m_Line == "\r")
			{
				// The client historically counts the terminating empty line.
				if (m_Mode == Mode::ClientRequest
					&& m_Lines >= I2P_TUNNEL_MAX_NUM_HTTP_HEADERS) return Reject ();
				if (m_Mode != Mode::ServerResponse && !m_Connection)
					m_Output += "Connection: close\r\n";
				if (m_Mode == Mode::ClientRequest && !m_ProxyConnection)
					m_Output += "Proxy-Connection: close\r\n";
				if (m_Mode == Mode::ServerRequest)
				{
					if (m_Output.size () > I2P_TUNNEL_HTTP_MAX_HEADER_SIZE
						|| m_Identity.size () > I2P_TUNNEL_HTTP_MAX_HEADER_SIZE - m_Output.size ()) return Reject ();
					m_Output += m_Identity;
				}
				m_Output += "\r\n";
				if (m_Output.size () > I2P_TUNNEL_HTTP_MAX_HEADER_SIZE) return Reject ();
				m_Output.append (input.substr (i + 1)); // body is binary, not header syntax
				m_Line.clear ();
				return m_Result = Result::Complete;
			}
			if (++m_Lines > I2P_TUNNEL_MAX_NUM_HTTP_HEADERS) return Reject ();
			TransformLine ();
			m_Line.clear ();
			if (m_Output.size () > I2P_TUNNEL_HTTP_MAX_HEADER_SIZE) return Reject ();
		}
		return m_Result;
	}

	const std::string & Output () const noexcept { return m_Output; }
	Result State () const noexcept { return m_Result; }

private:
	static char Lower (char ch) noexcept { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }
	static bool StartsWith (std::string_view text, std::string_view prefix) noexcept
	{
		if (text.size () < prefix.size ()) return false;
		for (size_t i = 0; i < prefix.size (); ++i)
			if (Lower (text[i]) != Lower (prefix[i])) return false;
		return true;
	}

	void TransformLine ()
	{
		if (m_Mode == Mode::ServerResponse)
		{
			// Preserve the existing response exclusion policy.
			for (auto prefix : std::array<std::string_view, 5> { "Server:", "Date:", "X-Runtime:", "X-Powered-By:", "Proxy" })
				if (std::string_view (m_Line).starts_with (prefix)) return;
		}
		else
		{
			if (m_Mode == Mode::ServerRequest)
			{
				if (StartsWith (m_Line, "Keep-Alive:") || StartsWith (m_Line, "X-I2P")) return;
				if (!m_Host.empty () && StartsWith (m_Line, "Host:"))
				{
					m_Output += "Host: " + m_Host + "\r\n";
					return;
				}
			}
			if (StartsWith (m_Line, "Connection:"))
			{
				m_Connection = true;
				// Retain websocket Upgrade handling across fragmented input.
				const auto pos = m_Line.find ("pgrade");
				if (pos != std::string::npos && pos && Lower (m_Line[pos - 1]) == 'u')
					m_Output += m_Line + '\n';
				else m_Output += "Connection: close\r\n";
				return;
			}
			if (m_Mode == Mode::ClientRequest && StartsWith (m_Line, "Proxy-Connection:"))
			{
				m_ProxyConnection = true;
				m_Output += "Proxy-Connection: close\r\n";
				return;
			}
		}
		m_Output += m_Line + '\n';
	}

	Result Reject () noexcept { return m_Result = Result::Rejected; }
	Mode m_Mode;
	Result m_Result = Result::Incomplete;
	std::string m_Host, m_Identity, m_Line, m_Output;
	size_t m_Bytes = 0, m_Lines = 0;
	bool m_Connection = false, m_ProxyConnection = false;
};
}
#endif
