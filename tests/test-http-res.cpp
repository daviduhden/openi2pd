/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test http res.
 *
 * Parses and serializes HTTP responses, covering status lines,
 * case-insensitive headers, duplicate/replace semantics, Content-Length
 * and chunked/gzip detection.
 *
 */

#include <cassert>
#include <cstring>
#include "HTTP.h"

using namespace i2p::http;

int main ()
{
	HTTPRes * res;
	int ret = 0, len = 0;
	const char * buf;

	/* test: parsing valid response without body */
	buf = "HTTP/1.1 304 Not Modified\r\n"
		  "Date: Thu, 14 Apr 2016 00:00:00 GMT\r\n"
		  "Server: nginx/1.2.1\r\n"
		  "Content-Length: 536\r\n"
		  "\r\n";
	len = strlen (buf);
	res = new HTTPRes;
	assert ((ret = res->parse (buf, len)) == len);
	assert (res->version == "HTTP/1.1");
	assert (res->status == "Not Modified");
	assert (res->code == 304);
	assert (res->headers.size () == 3);
	assert (res->headers.count ("Date") == 1);
	assert (res->headers.count ("Server") == 1);
	assert (res->headers.count ("Content-Length") == 1);
	assert (res->headers.find ("Date")->second == "Thu, 14 Apr 2016 00:00:00 GMT");
	assert (res->headers.find ("Server")->second == "nginx/1.2.1");
	assert (res->headers.find ("Content-Length")->second == "536");
	assert (res->is_chunked () == false);
	assert (res->content_length () == 536);
	delete res;

	/* test: building request */
	buf = "HTTP/1.0 304 Not Modified\r\n"
		  "Content-Length: 0\r\n"
		  "\r\n";
	res = new HTTPRes;
	res->version = "HTTP/1.0";
	res->code = 304;
	res->status = "Not Modified";
	res->add_header ("Content-Length", "0");
	assert (res->to_string () == buf);
	delete res;

	/* test: case-insensitive header parsing (RFC 7230) */
	buf = "HTTP/1.1 200 OK\r\n"
		  "content-type: text/html\r\n"
		  "CONTENT-LENGTH: 42\r\n"
		  "transfer-encoding: chunked\r\n"
		  "content-encoding: gzip\r\n"
		  "\r\n";
	len = strlen (buf);
	res = new HTTPRes;
	assert ((ret = res->parse (buf, len)) == len);
	assert (res->code == 200);
	/* content_length() should work with any case */
	assert (res->content_length () == 42);
	/* is_chunked() should work with any case */
	assert (res->is_chunked () == true);
	/* is_gzipped() should work with any case */
	assert (res->is_gzipped () == true);
	delete res;

	/* test: add_header case-insensitivity (no duplicates) */
	res = new HTTPRes;
	res->add_header ("Content-Type", "text/plain");
	res->add_header ("content-type", "text/html"); /* should not add duplicate */
	assert (res->headers.size () == 1);
	res->add_header ("content-type", "application/json", true); /* replace=true */
	assert (res->headers.size () == 1);
	delete res;

	/* test: del_header case-insensitivity */
	res = new HTTPRes;
	res->add_header ("X-Custom-Header", "value");
	assert (res->headers.size () == 1);
	res->del_header ("x-custom-header");
	assert (res->headers.size () == 0);
	delete res;

	// Serialization must not duplicate Date/Content-Length by case.
	res = new HTTPRes;
	res->version = "HTTP/1.1";
	res->code = 200;
	res->status = "OK";
	res->add_header ("date", "Thu, 01 Jan 1970 00:00:00 GMT");
	res->add_header ("content-length", "0");
	res->body = "";
	std::string response = res->to_string ();
	/* should not have duplicate headers */
	size_t date_count = 0, pos = 0;
	while ((pos = response.find ("ate:", pos)) != std::string::npos)
	{
		date_count++;
		pos++;
	}
	assert (date_count == 1); /* only one Date header */
	delete res;

	// Reseed uses case-insensitive Location lookup for redirects.
	buf = "HTTP/1.1 302 Found\r\n"
		  "location: https://example.com/new\r\n"
		  "\r\n";
	len = strlen (buf);
	res = new HTTPRes;
	assert ((ret = res->parse (buf, len)) == len);
	assert (res->get_header ("Location") == "https://example.com/new");
	assert (res->get_header ("location") == "https://example.com/new");
	assert (res->get_header ("LOCATION") == "https://example.com/new");
	delete res;

	/* header values must not smuggle control characters or bare LF */
	const char * injected = "HTTP/1.1 200 OK\r\nX-Test: value\nInjected: yes\r\n\r\n";
	HTTPRes injectedResponse;
	assert (injectedResponse.parse (injected, strlen (injected)) == -1);

	/* proxy error statuses must have stable reason phrases */
	assert (HTTPCodeToStatus (400) == "Bad Request");
	assert (HTTPCodeToStatus (431) == "Request Header Fields Too Large");
	assert (HTTPCodeToStatus (500) == "Internal Server Error");
	assert (HTTPCodeToStatus (502) == "Bad Gateway");
	assert (HTTPCodeToStatus (504) == "Gateway Timeout");
	for (int code : { 400, 431, 500, 502, 504 })
	{
		HTTPRes proxyError;
		proxyError.version = "HTTP/1.1";
		proxyError.code = code;
		proxyError.body = "proxy error";
		const std::string proxyErrorText = proxyError.to_string ();
		assert (proxyErrorText.starts_with (
			"HTTP/1.1 " + std::to_string (code) + " " + std::string (HTTPCodeToStatus (code)) + "\r\n"));
		assert (proxyErrorText.find ("Content-Length: 11\r\n") != std::string::npos);
	}

	return 0;
}

/* vim: expandtab:ts=2 */
