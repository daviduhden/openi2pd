/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: HTTPProxy.
 *
 * Declares the HTTP proxy acceptor and the hard 1 MiB cap for an incomplete
 * request header, checked before every append to bound client buffering.
 *
 */

#ifndef HTTP_PROXY_H__
#define HTTP_PROXY_H__

#include <cstddef>

namespace i2p
{
namespace proxy
{
	/**
	 * Hard ceiling for an incomplete HTTP request header accumulated from a
	 * client. It bounds m_recv_buf while the handler is still searching for
	 * the end of headers. The limit is checked before each append, so a peer
	 * cannot grow the buffer past it by sending one byte at a time.
	 */
	constexpr std::size_t HTTP_PROXY_MAX_HEADER_SIZE = 1024 * 1024; // 1 MiB

	/**
	 * @return true when appending @a incoming bytes to a header that has no
	 * end-of-headers marker yet would exceed HTTP_PROXY_MAX_HEADER_SIZE.
	 * Only call this while the buffered header is still incomplete.
	 */
	inline bool HTTPProxyHeaderLimitExceeded (std::size_t buffered, std::size_t incoming) noexcept
	{
		if (buffered > HTTP_PROXY_MAX_HEADER_SIZE) return true;
		return incoming > HTTP_PROXY_MAX_HEADER_SIZE - buffered;
	}

	class HTTPProxy : public i2p::client::TCPIPAcceptor
	{
	public:
		HTTPProxy (const std::string & name, const std::string & address, uint16_t port, const std::string & outproxy,
			bool addresshelper, bool senduseragent, std::shared_ptr<i2p::client::ClientDestination> localDestination);
		HTTPProxy (const std::string & name, const std::string & address, uint16_t port,
			std::shared_ptr<i2p::client::ClientDestination> localDestination = nullptr)
			: HTTPProxy (name, address, port, "", true, false, localDestination) { };
		~HTTPProxy () { };

		// picks the next outproxy, so several of them are used in turn
		std::string GetOutproxyURL ();
		bool GetHelperSupport () const { return m_Addresshelper; }
		bool GetSendUserAgent () const { return m_SendUserAgent; }

	protected:
		// Implements TCPIPAcceptor
		std::shared_ptr<i2p::client::I2PServiceHandler> CreateHandler (
			std::shared_ptr<boost::asio::ip::tcp::socket> socket) override;
		const char * GetName () const override { return m_Name.c_str (); }

	private:
		std::string m_Name;
		std::vector<std::string> m_OutproxyUrls;
		size_t m_NextOutproxy;
		bool m_Addresshelper, m_SendUserAgent;
	};
} // http
} // i2p

#endif
