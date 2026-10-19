/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <cstdio>
#include <memory>
#include <sstream>
#include <iomanip>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string.hpp>

#include "FS.h"
#include "Base.h"
#include "Log.h"
#include "Config.h"
#include "NetDb.hpp"
#include "Tunnel.h"
#include "Daemon.h"
#include "I2PControl.h"

namespace
{
	struct FileCloser
	{
		void operator() (FILE * file) const
		{
			if (file) std::fclose (file);
		}
	};
	using FilePtr = std::unique_ptr<FILE, FileCloser>;

	// Return the first queued OpenSSL error and drain the queue, so a later
	// unrelated failure is not reported with a stale reason.
	std::string GetOpenSSLError ()
	{
		unsigned long error = ERR_get_error ();
		if (!error) return "unknown error";
		char buffer[256];
		ERR_error_string_n (error, buffer, sizeof (buffer));
		while (ERR_get_error () != 0) { }
		return buffer;
	}
}

namespace i2p
{
namespace client
{
	I2PControlService::I2PControlService (const std::string & address, int port)
		: m_IsRunning (false)
		, m_SSLContext (boost::asio::ssl::context::sslv23)
		, m_ShutdownTimer (m_Service)
	{
		if (port)
			m_Acceptor = std::make_unique<boost::asio::ip::tcp::acceptor> (
				m_Service, boost::asio::ip::tcp::endpoint (boost::asio::ip::make_address (address), port));
		else
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		{
			std::remove (address.c_str ()); // just in case
			m_LocalAcceptor = std::make_unique<boost::asio::local::stream_protocol::acceptor> (
				m_Service, boost::asio::local::stream_protocol::endpoint (address));
		}
#else
			LogPrint (eLogError, "I2PControl: Local sockets are not supported");
#endif

		i2p::config::GetOption ("i2pcontrol.password", m_Password);

		// certificate / keys
		std::string i2pcp_crt;
		i2p::config::GetOption ("i2pcontrol.cert", i2pcp_crt);
		std::string i2pcp_key;
		i2p::config::GetOption ("i2pcontrol.key", i2pcp_key);

		if (!i2pcp_crt.empty () && i2pcp_crt[0] != '/') i2pcp_crt = i2p::fs::DataDirPath (i2pcp_crt);
		if (!i2pcp_key.empty () && i2pcp_key[0] != '/') i2pcp_key = i2p::fs::DataDirPath (i2pcp_key);
		if (!i2p::fs::Exists (i2pcp_crt) || !i2p::fs::Exists (i2pcp_key))
		{
			LogPrint (eLogInfo, "I2PControl: Creating new certificate for control connection ", i2pcp_crt, " key ",
				i2pcp_key);
			CreateCertificate (i2pcp_crt.c_str (), i2pcp_key.c_str ());
		}
		else
			LogPrint (eLogDebug, "I2PControl: Using cert from ", i2pcp_crt, " key ", i2pcp_key);
		m_SSLContext.set_options (boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2
			| boost::asio::ssl::context::single_dh_use);
		boost::system::error_code ec;
		m_SSLContext.use_certificate_file (i2pcp_crt, boost::asio::ssl::context::pem, ec);
		if (!ec) m_SSLContext.use_private_key_file (i2pcp_key, boost::asio::ssl::context::pem, ec);
		if (ec)
		{
			LogPrint (eLogInfo, "I2PControl: Failed to load ceritifcate: ", ec.message (), ". Recreating");
			CreateCertificate (i2pcp_crt.c_str (), i2pcp_key.c_str ());
			m_SSLContext.use_certificate_file (i2pcp_crt, boost::asio::ssl::context::pem, ec);
			if (!ec) m_SSLContext.use_private_key_file (i2pcp_key, boost::asio::ssl::context::pem, ec);
			if (ec)
				// give up
				LogPrint (eLogError, "I2PControl: Can't load certificates");
		}

		// handlers
		m_MethodHandlers["Authenticate"] = &I2PControlService::AuthenticateHandler;
		m_MethodHandlers["Echo"] = &I2PControlService::EchoHandler;
		m_MethodHandlers["I2PControl"] = &I2PControlService::I2PControlHandler;
		m_MethodHandlers["RouterInfo"] = &I2PControlHandlers::RouterInfoHandler;
		m_MethodHandlers["RouterManager"] = &I2PControlService::RouterManagerHandler;
		m_MethodHandlers["NetworkSetting"] = &I2PControlHandlers::NetworkSettingHandler;
		m_MethodHandlers["ClientServicesInfo"] = &I2PControlHandlers::ClientServicesInfoHandler;
		m_MethodHandlers["LocalDestinationInfo"] = &I2PControlHandlers::LocalDestinationInfoHandler;

		// I2PControl
		m_I2PControlHandlers["i2pcontrol.password"] = &I2PControlService::PasswordHandler;

		// RouterManager
		m_RouterManagerHandlers["Reseed"] = &I2PControlService::ReseedHandler;
		m_RouterManagerHandlers["Shutdown"] = &I2PControlService::ShutdownHandler;
		m_RouterManagerHandlers["ShutdownGraceful"] = &I2PControlService::ShutdownGracefulHandler;
	}

	I2PControlService::~I2PControlService ()
	{
		Stop ();
	}

	void I2PControlService::Start ()
	{
		if (!m_IsRunning)
		{
			Accept ();
			m_IsRunning = true;
			m_Thread = std::make_unique<std::thread> (std::bind (&I2PControlService::Run, this));
		}
	}

	void I2PControlService::Stop ()
	{
		if (m_IsRunning)
		{
			m_IsRunning = false;
			if (m_Acceptor) m_Acceptor->cancel ();
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
			if (m_LocalAcceptor)
			{
				auto path = m_LocalAcceptor->local_endpoint ().path ();
				m_LocalAcceptor->cancel ();
				std::remove (path.c_str ());
			}
#endif
			m_Service.stop ();
			if (m_Thread)
			{
				m_Thread->join ();
				m_Thread = nullptr;
			}
		}
	}

	void I2PControlService::Run ()
	{
		i2p::util::SetThreadName ("I2PC");

		while (m_IsRunning)
		{
			try
			{
				m_Service.run ();
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogError, "I2PControl: Runtime exception: ", ex.what ());
			}
		}
	}

	void I2PControlService::Accept ()
	{
		if (m_Acceptor)
		{
			auto newSocket
				= std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> (m_Service, m_SSLContext);
			m_Acceptor->async_accept (newSocket->lowest_layer (),
				[this, newSocket] (const boost::system::error_code & ecode) { HandleAccepted (ecode, newSocket); });
		}
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		else if (m_LocalAcceptor)
		{
			auto newSocket = std::make_shared<boost::asio::ssl::stream<boost::asio::local::stream_protocol::socket>> (
				m_Service, m_SSLContext);
			m_LocalAcceptor->async_accept (newSocket->lowest_layer (),
				[this, newSocket] (const boost::system::error_code & ecode) { HandleAccepted (ecode, newSocket); });
		}
#endif
	}

	template <typename ssl_socket>
	void I2PControlService::HandleAccepted (
		const boost::system::error_code & ecode, std::shared_ptr<ssl_socket> newSocket)
	{
		if (ecode != boost::asio::error::operation_aborted) Accept ();

		if (ecode)
		{
			LogPrint (eLogError, "I2PControl: Accept error: ", ecode.message ());
			return;
		}
		LogPrint (eLogDebug, "I2PControl: New request from ", newSocket->lowest_layer ().remote_endpoint ());
		Handshake (newSocket);
	}

	template <typename ssl_socket> void I2PControlService::Handshake (std::shared_ptr<ssl_socket> socket)
	{
		socket->async_handshake (boost::asio::ssl::stream_base::server,
			[this, socket] (const boost::system::error_code & ecode)
			{
				if (ecode)
				{
					LogPrint (eLogError, "I2PControl: Handshake error: ", ecode.message ());
					return;
				}
				ReadRequest (socket);
			});
	}

	template <typename ssl_socket> void I2PControlService::ReadRequest (std::shared_ptr<ssl_socket> socket)
	{
		auto request = std::make_shared<I2PControlBuffer> ();
		socket->async_read_some (boost::asio::buffer (*request),
			[this, socket, request] (const boost::system::error_code & ecode, size_t bytes_transferred)
			{ HandleRequestReceived (ecode, bytes_transferred, socket, request); });
	}

	template <typename ssl_socket>
	void I2PControlService::HandleRequestReceived (const boost::system::error_code & ecode, size_t bytes_transferred,
		std::shared_ptr<ssl_socket> socket, std::shared_ptr<I2PControlBuffer> buf)
	{
		if (ecode)
		{
			LogPrint (eLogError, "I2PControl: Read error: ", ecode.message ());
			return;
		}
		else
		{
			bool isHtml = !memcmp (buf->data (), "POST", 4);
			try
			{
				std::stringstream ss;
				ss.write (buf->data (), bytes_transferred);
				if (isHtml)
				{
					std::string header;
					size_t contentLength = 0;
					while (!ss.eof () && header != "\r")
					{
						std::getline (ss, header);
						auto colon = header.find (':');
						if (colon != std::string::npos && boost::iequals (header.substr (0, colon), "Content-Length"))
							contentLength = std::stoi (header.substr (colon + 1));
					}
					if (ss.eof ())
					{
						LogPrint (eLogError, "I2PControl: Malformed request, HTTP header expected");
						return; // TODO:
					}
					if (contentLength > I2P_CONTROL_MAX_CONTENT_LENGTH)
					{
						LogPrint (
							eLogError, "I2PControl: Content-Length ", contentLength, " exceeds maximum allowed size");
						return;
					}
					std::streamoff rem = contentLength + ss.tellg () - bytes_transferred; // more bytes to read
					while (rem > 0) // read in chunks to prevent buffer overflow
					{
						size_t toRead = std::min ((size_t)rem, buf->size ()); // don't exceed buffer size
						bytes_transferred = boost::asio::read (*socket, boost::asio::buffer (buf->data (), toRead));
						if (bytes_transferred == 0) break;
						ss.write (buf->data (), bytes_transferred);
						rem -= bytes_transferred;
					}
				}
				std::ostringstream response;
				boost::property_tree::ptree pt;
				boost::property_tree::read_json (ss, pt);

				std::string id = pt.get<std::string> ("id");
				std::string method = pt.get<std::string> ("method");
				auto it = m_MethodHandlers.find (method);
				if (it != m_MethodHandlers.end ())
				{
					auto params = pt.get_child ("params");
					if (method != "Authenticate" && !IsValidToken (params.get<std::string> ("Token", "")))
					{
						LogPrint (eLogWarning, "I2PControl: Invalid or missing token for method ", method);
						response << "{\"id\":null,\"error\":";
						response << "{\"code\":-32602,\"message\":\"Invalid or missing token\"},";
						response << "\"jsonrpc\":\"2.0\"}\n";
					}
					else
					{
						response << "{\"id\":" << id << ",\"result\":{";
						(this->*(it->second)) (params, response);
						response << "},\"jsonrpc\":\"2.0\"}\n";
					}
				}
				else
				{
					LogPrint (eLogWarning, "I2PControl: Unknown method ", method);
					response << "{\"id\":null,\"error\":";
					response << "{\"code\":-32601,\"message\":\"Method not found\"},";
					response << "\"jsonrpc\":\"2.0\"}\n";
				}
				SendResponse (socket, response, isHtml);
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogError, "I2PControl: Exception when handle request: ", ex.what ());
				std::ostringstream response;
				response << "{\"id\":null,\"error\":";
				response << "{\"code\":-32700,\"message\":\"" << ex.what () << "\"},";
				response << "\"jsonrpc\":\"2.0\"}\n";
				SendResponse (socket, response, isHtml);
			}
			catch (...)
			{
				LogPrint (eLogError, "I2PControl: Handle request unknown exception");
			}
		}
	}

	template <typename ssl_socket>
	void I2PControlService::SendResponse (
		std::shared_ptr<ssl_socket> socket, std::ostringstream & response, bool isHtml)
	{
		auto message = std::make_shared<std::string> ();
		if (isHtml)
		{
			std::ostringstream header;
			header << "HTTP/1.1 200 OK\r\n";
			header << "Connection: close\r\n";
			header << "Content-Length: " << std::to_string (response.str ().length ()) << "\r\n";
			header << "Content-Type: application/json\r\n";
			header << "Date: ";
			std::time_t t = std::time (nullptr);
			std::tm tm = *std::gmtime (&t);
			header << std::put_time (&tm, "%a, %d %b %Y %T GMT") << "\r\n";
			header << "\r\n";
			*message = header.str ();
		}
		*message += response.str ();
		boost::asio::async_write (*socket, boost::asio::buffer (*message), boost::asio::transfer_all (),
			[socket, message] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{
				if (ecode) LogPrint (eLogError, "I2PControl: Write error: ", ecode.message ());
			});
	}

	// handlers

	void I2PControlService::AuthenticateHandler (
		const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		int api = params.get<int> ("API");
		auto password = params.get<std::string> ("Password");
		LogPrint (eLogDebug, "I2PControl: Authenticate API=", api);
		bool isValid = password.size () == m_Password.size ()
			&& CRYPTO_memcmp (password.data (), m_Password.data (), m_Password.size ()) == 0;
		if (!isValid)
		{
			LogPrint (eLogError, "I2PControl: Authenticate - Invalid password");
			return;
		}
		InsertParam (results, "API", api);
		results << ",";
		uint8_t tokenBytes[32];
		RAND_bytes (tokenBytes, 32);
		std::string token = i2p::data::ByteStreamToBase64 (tokenBytes, 32);
		m_Tokens[token] = i2p::util::GetSecondsSinceEpoch () + I2P_CONTROL_TOKEN_LIFETIME;
		InsertParam (results, "Token", token);
	}

	bool I2PControlService::IsValidToken (const std::string & token)
	{
		uint64_t ts = i2p::util::GetSecondsSinceEpoch ();
		for (auto it = m_Tokens.begin (); it != m_Tokens.end ();)
		{
			if (ts > it->second)
				it = m_Tokens.erase (it); // expired
			else
				++it;
		}
		bool found = false;
		for (const auto & it : m_Tokens)
		{
			if (it.first.size () == token.size ()
				&& CRYPTO_memcmp (it.first.data (), token.data (), token.size ()) == 0)
				found = true; // don't short-circuit; keep timing independent of match position
		}
		return found;
	}

	void I2PControlService::EchoHandler (const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		auto echo = params.get<std::string> ("Echo");
		LogPrint (eLogDebug, "I2PControl Echo Echo=", echo);
		InsertParam (results, "Result", echo);
	}

	// I2PControl

	void I2PControlService::I2PControlHandler (const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		for (auto & it : params)
		{
			LogPrint (eLogDebug, "I2PControl: I2PControl request: ", it.first);
			auto it1 = m_I2PControlHandlers.find (it.first);
			if (it1 != m_I2PControlHandlers.end ())
			{
				(this->*(it1->second)) (it.second.data ());
				InsertParam (results, it.first, "");
			}
			else
				LogPrint (eLogError, "I2PControl: I2PControl unknown request: ", it.first);
		}
	}

	void I2PControlService::PasswordHandler (const std::string & value)
	{
		LogPrint (
			eLogWarning, "I2PControl: New password=", value, ", to make it persistent you should update your config!");
		m_Password = value;
		m_Tokens.clear ();
	}

	// RouterManager

	void I2PControlService::RouterManagerHandler (
		const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		for (auto it = params.begin (); it != params.end (); it++)
		{
			LogPrint (eLogDebug, "I2PControl: RouterManager request: ", it->first);
			auto it1 = m_RouterManagerHandlers.find (it->first);
			if (it1 != m_RouterManagerHandlers.end ())
			{
				if (it != params.begin ()) results << ",";
				(this->*(it1->second)) (results);
			}
			else
				LogPrint (eLogError, "I2PControl: RouterManager unknown request: ", it->first);
		}
	}

	void I2PControlService::ShutdownHandler (std::ostringstream & results)
	{
		LogPrint (eLogInfo, "I2PControl: Shutdown requested");
		InsertParam (results, "Shutdown", "");
		m_ShutdownTimer.expires_after (std::chrono::seconds (1)); // 1 second to make sure response has been sent
		m_ShutdownTimer.async_wait ([] (const boost::system::error_code & ecode) { Daemon.running = 0; });
	}

	void I2PControlService::ShutdownGracefulHandler (std::ostringstream & results)
	{
		i2p::context.SetAcceptsTunnels (false);
		int timeout = i2p::tunnel::tunnels.GetTransitTunnelsExpirationTimeout ();
		LogPrint (eLogInfo, "I2PControl: Graceful shutdown requested, ", timeout, " seconds remains");
		InsertParam (results, "ShutdownGraceful", "");
		m_ShutdownTimer.expires_after (std::chrono::seconds (timeout + 1)); // + 1 second
		m_ShutdownTimer.async_wait ([] (const boost::system::error_code & ecode) { Daemon.running = 0; });
	}

	void I2PControlService::ReseedHandler (std::ostringstream & results)
	{
		LogPrint (eLogInfo, "I2PControl: Reseed requested");
		InsertParam (results, "Reseed", "");
		i2p::data::netdb.Reseed ();
	}

	// certificate
	void I2PControlService::CreateCertificate (const char * crt_path, const char * key_path)
	{
		std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> pkey (nullptr, EVP_PKEY_free);
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
		pkey.reset (EVP_RSA_gen (4096)); // e = 65537
		if (!pkey)
		{
			LogPrint (eLogError, "I2PControl: Can't create RSA key for certificate: ", GetOpenSSLError ());
			return;
		}
#else
		pkey.reset (EVP_PKEY_new ());
		std::unique_ptr<RSA, decltype (&RSA_free)> rsa (RSA_new (), RSA_free);
		std::unique_ptr<BIGNUM, decltype (&BN_free)> e (BN_dup (i2p::crypto::GetRSAE ()), BN_free);
		if (!pkey || !rsa || !e || RSA_generate_key_ex (rsa.get (), 4096, e.get (), nullptr) != 1)
		{
			LogPrint (eLogError, "I2PControl: Can't create RSA key for certificate: ", GetOpenSSLError ());
			return;
		}
		if (EVP_PKEY_assign_RSA (pkey.get (), rsa.get ()) != 1)
		{
			LogPrint (eLogError, "I2PControl: Can't assign RSA key for certificate: ", GetOpenSSLError ());
			return;
		}
		rsa.release (); // pkey owns the RSA key now
#endif

		// The certificate is released after it has been written and before the
		// private key is saved, matching the previous explicit free order.
		{
			std::unique_ptr<X509, decltype (&X509_free)> x509 (X509_new (), X509_free);
			if (!x509)
			{
				LogPrint (eLogError, "I2PControl: Can't create certificate: ", GetOpenSSLError ());
				return;
			}
			if (ASN1_INTEGER_set (X509_get_serialNumber (x509.get ()), 1) != 1
				|| !X509_gmtime_adj (X509_getm_notBefore (x509.get ()), 0)
				|| !X509_gmtime_adj (X509_getm_notAfter (x509.get ()),
					I2P_CONTROL_CERTIFICATE_VALIDITY * 24 * 60 * 60) // expiration
				|| X509_set_pubkey (x509.get (), pkey.get ()) != 1) // public key
			{
				LogPrint (eLogError, "I2PControl: Can't initialize certificate: ", GetOpenSSLError ());
				return;
			}

			// The subject is a private copy; it is released right after signing.
			{
				std::unique_ptr<X509_NAME, decltype (&X509_NAME_free)> name (
					X509_NAME_dup (X509_get_subject_name (x509.get ())), X509_NAME_free);
				if (!name)
				{
					LogPrint (eLogError, "I2PControl: Can't duplicate certificate subject: ", GetOpenSSLError ());
					return;
				}
				if (X509_NAME_add_entry_by_txt (name.get (), "C", MBSTRING_ASC, (unsigned char *)"A1", -1, -1, 0)
						!= 1 // country (Anonymous proxy)
					|| X509_NAME_add_entry_by_txt (name.get (), "O", MBSTRING_ASC,
						   (unsigned char *)I2P_CONTROL_CERTIFICATE_ORGANIZATION, -1, -1, 0)
						!= 1 // organization
					|| X509_NAME_add_entry_by_txt (name.get (), "CN", MBSTRING_ASC,
						   (unsigned char *)I2P_CONTROL_CERTIFICATE_COMMON_NAME, -1, -1, 0)
						!= 1) // common name
				{
					LogPrint (eLogError, "I2PControl: Can't set certificate subject: ", GetOpenSSLError ());
					return;
				}
				if (X509_set_subject_name (x509.get (), name.get ()) != 1
					|| X509_set_issuer_name (x509.get (), name.get ()) != 1) // set issuer to ourselves
				{
					LogPrint (eLogError, "I2PControl: Can't set certificate names: ", GetOpenSSLError ());
					return;
				}
				if (X509_sign (x509.get (), pkey.get (), EVP_sha1 ()) == 0) // last param must be NULL for EdDSA
				{
					LogPrint (eLogError, "I2PControl: Can't sign certificate: ", GetOpenSSLError ());
					return;
				}
			}

			// save cert
			FilePtr f (fopen (crt_path, "wb"));
			if (f)
			{
				LogPrint (eLogInfo, "I2PControl: Saving new cert to ", crt_path);
				if (PEM_write_X509 (f.get (), x509.get ()) != 1)
					LogPrint (eLogError, "I2PControl: Can't write cert: ", GetOpenSSLError ());
				else if (std::fclose (f.release ()) != 0)
					LogPrint (eLogError, "I2PControl: Can't close cert: ", strerror (errno));
			}
			else
				LogPrint (eLogError, "I2PControl: Can't write cert: ", strerror (errno));
		}

		// save key
		i2p::fs::CreatePrivateFile (key_path);
		FilePtr f (fopen (key_path, "wb"));
		if (f)
		{
			LogPrint (eLogInfo, "I2PControl: saving cert key to ", key_path);
			if (PEM_write_PrivateKey (f.get (), pkey.get (), NULL, NULL, 0, NULL, NULL) != 1)
				LogPrint (eLogError, "I2PControl: Can't write key: ", GetOpenSSLError ());
			else if (std::fclose (f.release ()) != 0)
				LogPrint (eLogError, "I2PControl: Can't close key: ", strerror (errno));
		}
		else
			LogPrint (eLogError, "I2PControl: Can't write key: ", strerror (errno));
	}
}
}
