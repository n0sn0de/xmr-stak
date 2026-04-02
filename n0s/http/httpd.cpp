/*
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  * Additional permission under GNU GPL version 3 section 7
  *
  * If you modify this Program, or any covered work, by linking or combining
  * it with OpenSSL (or a modified version of that library), containing parts
  * covered by the terms of OpenSSL License and SSLeay License, the licensors
  * of this Program grant you additional permission to convey the resulting work.
  *
  */

#ifndef CONF_NO_HTTPD

#include "httpd.hpp"
#include "embedded_assets.hpp"
#include "webdesign.hpp"
#include "n0s/jconf.hpp"
#include "n0s/misc/console.hpp"
#include "n0s/misc/executor.hpp"
#include "n0s/net/msgstruct.hpp"
#include "n0s/params.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sstream>

#include <microhttpd.h>
httpd* httpd::oInst = nullptr;

httpd::httpd()
{
}

MHD_Result httpd::req_handler([[maybe_unused]] void* cls,
	MHD_Connection* connection,
	const char* url,
	const char* method,
	[[maybe_unused]] const char* version,
	[[maybe_unused]] const char* upload_data,
	[[maybe_unused]] size_t* upload_data_size,
	void** ptr)
{
	struct MHD_Response* rsp;

	if(strcmp(method, "GET") != 0)
		return MHD_NO;

	if(strlen(jconf::inst()->GetHttpUsername()) != 0)
	{
		char* username;
		MHD_Result ret;

		username = MHD_digest_auth_get_username(connection);
		if(username == nullptr)
		{
			rsp = MHD_create_response_from_buffer(sHtmlAccessDeniedSize, const_cast<char*>(sHtmlAccessDenied), MHD_RESPMEM_PERSISTENT);
			ret = MHD_queue_auth_fail_response(connection, sHttpAuthRealm, sHttpAuthOpaque, rsp, MHD_NO);
			MHD_destroy_response(rsp);
			return ret;
		}
		free(username);

		ret = (MHD_Result)MHD_digest_auth_check(connection, sHttpAuthRealm, jconf::inst()->GetHttpUsername(), jconf::inst()->GetHttpPassword(), 300);
		if(ret == MHD_INVALID_NONCE || ret == MHD_NO)
		{
			rsp = MHD_create_response_from_buffer(sHtmlAccessDeniedSize, const_cast<char*>(sHtmlAccessDenied), MHD_RESPMEM_PERSISTENT);
			ret = MHD_queue_auth_fail_response(connection, sHttpAuthRealm, sHttpAuthOpaque, rsp, (ret == MHD_INVALID_NONCE) ? MHD_YES : MHD_NO);
			MHD_destroy_response(rsp);
			return ret;
		}
	}

	*ptr = nullptr;
	std::string str;

	// Helper lambda: serve JSON API response with CORS headers
	auto serve_api_json = [&](ex_event_name ev) -> MHD_Result {
		executor::inst()->get_http_report(ev, str);
		rsp = MHD_create_response_from_buffer(str.size(), const_cast<char*>(str.c_str()), MHD_RESPMEM_MUST_COPY);
		MHD_add_response_header(rsp, "Content-Type", "application/json; charset=utf-8");
		MHD_add_response_header(rsp, "Access-Control-Allow-Origin", "*");
		MHD_add_response_header(rsp, "Cache-Control", "no-cache");
		MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, rsp);
		MHD_destroy_response(rsp);
		return ret;
	};

	// REST API v1 endpoints
	if(strncasecmp(url, "/api/v1/", 8) == 0)
	{
		const char* endpoint = url + 8;
		if(strcasecmp(endpoint, "status") == 0)
			return serve_api_json(EV_API_STATUS);
		else if(strcasecmp(endpoint, "hashrate/history") == 0)
			return serve_api_json(EV_API_HASHRATE_HISTORY);
		else if(strcasecmp(endpoint, "hashrate") == 0)
			return serve_api_json(EV_API_HASHRATE);
		else if(strcasecmp(endpoint, "gpus") == 0)
			return serve_api_json(EV_API_GPUS);
		else if(strcasecmp(endpoint, "pool") == 0)
			return serve_api_json(EV_API_POOL);
		else if(strcasecmp(endpoint, "version") == 0)
			return serve_api_json(EV_API_VERSION);
		else
		{
			// Unknown API endpoint — 404
			const char* notFound = "{\"error\":\"not_found\"}";
			rsp = MHD_create_response_from_buffer(strlen(notFound), const_cast<char*>(notFound), MHD_RESPMEM_PERSISTENT);
			MHD_add_response_header(rsp, "Content-Type", "application/json; charset=utf-8");
			MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, rsp);
			MHD_destroy_response(rsp);
			return ret;
		}
	}
	// GUI dashboard assets: /gui/* and root /
	else if(strcasecmp(url, "/") == 0 || strcasecmp(url, "/gui") == 0 || strcasecmp(url, "/gui/") == 0)
	{
		// Redirect to /gui/index.html
		rsp = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(rsp, "Location", "/gui/index.html");
		MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_TEMPORARY_REDIRECT, rsp);
		MHD_destroy_response(rsp);
		return ret;
	}
	else if(strncasecmp(url, "/gui/", 5) == 0)
	{
		// Dev mode: serve from filesystem
		if(n0s::params::inst().guiDev)
		{
			std::string filepath = n0s::params::inst().guiDevPath + "/" + (url + 5);
			std::ifstream file(filepath, std::ios::binary);
			if(file.good())
			{
				std::ostringstream ss;
				ss << file.rdbuf();
				str = ss.str();

				const char* ctype = "application/octet-stream";
				if(filepath.find(".html") != std::string::npos) ctype = "text/html; charset=utf-8";
				else if(filepath.find(".css") != std::string::npos) ctype = "text/css; charset=utf-8";
				else if(filepath.find(".js") != std::string::npos) ctype = "application/javascript; charset=utf-8";

				rsp = MHD_create_response_from_buffer(str.size(), const_cast<char*>(str.c_str()), MHD_RESPMEM_MUST_COPY);
				MHD_add_response_header(rsp, "Content-Type", ctype);
				MHD_add_response_header(rsp, "Cache-Control", "no-cache");
				MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, rsp);
				MHD_destroy_response(rsp);
				return ret;
			}
		}

		// Embedded mode: serve pre-gzipped assets
		const auto* asset = n0s::gui::findAsset(url);
		if(asset != nullptr)
		{
			rsp = MHD_create_response_from_buffer(asset->size,
				const_cast<uint8_t*>(asset->data), MHD_RESPMEM_PERSISTENT);
			MHD_add_response_header(rsp, "Content-Type", asset->content_type);
			MHD_add_response_header(rsp, "Content-Encoding", "gzip");
			MHD_add_response_header(rsp, "Cache-Control", "public, max-age=86400");
			MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, rsp);
			MHD_destroy_response(rsp);
			return ret;
		}

		// 404 for unknown GUI paths
		const char* notFound = "<h1>404 Not Found</h1>";
		rsp = MHD_create_response_from_buffer(strlen(notFound), const_cast<char*>(notFound), MHD_RESPMEM_PERSISTENT);
		MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, rsp);
		MHD_destroy_response(rsp);
		return ret;
	}
	// Legacy endpoints preserved
	else if(strcasecmp(url, "/style.css") == 0)
	{
		const char* req_etag = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "If-None-Match");

		if(req_etag != nullptr && strcmp(req_etag, sHtmlCssEtag) == 0)
		{
			rsp = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
			MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_MODIFIED, rsp);
			MHD_destroy_response(rsp);
			return ret;
		}

		rsp = MHD_create_response_from_buffer(sHtmlCssSize, const_cast<char*>(sHtmlCssFile), MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(rsp, "ETag", sHtmlCssEtag);
		MHD_add_response_header(rsp, "Content-Type", "text/css; charset=utf-8");
	}
	else if(strcasecmp(url, "/api.json") == 0)
	{
		executor::inst()->get_http_report(EV_HTML_JSON, str);

		rsp = MHD_create_response_from_buffer(str.size(), const_cast<char*>(str.c_str()), MHD_RESPMEM_MUST_COPY);
		MHD_add_response_header(rsp, "Content-Type", "application/json; charset=utf-8");
	}
	else if(strcasecmp(url, "/h") == 0 || strcasecmp(url, "/hashrate") == 0)
	{
		executor::inst()->get_http_report(EV_HTML_HASHRATE, str);

		rsp = MHD_create_response_from_buffer(str.size(), const_cast<char*>(str.c_str()), MHD_RESPMEM_MUST_COPY);
		MHD_add_response_header(rsp, "Content-Type", "text/html; charset=utf-8");
	}
	else if(strcasecmp(url, "/c") == 0 || strcasecmp(url, "/connection") == 0)
	{
		executor::inst()->get_http_report(EV_HTML_CONNSTAT, str);

		rsp = MHD_create_response_from_buffer(str.size(), const_cast<char*>(str.c_str()), MHD_RESPMEM_MUST_COPY);
		MHD_add_response_header(rsp, "Content-Type", "text/html; charset=utf-8");
	}
	else if(strcasecmp(url, "/r") == 0 || strcasecmp(url, "/results") == 0)
	{
		executor::inst()->get_http_report(EV_HTML_RESULTS, str);

		rsp = MHD_create_response_from_buffer(str.size(), const_cast<char*>(str.c_str()), MHD_RESPMEM_MUST_COPY);
		MHD_add_response_header(rsp, "Content-Type", "text/html; charset=utf-8");
	}
	else
	{
		// Unknown path — redirect to GUI dashboard
		rsp = MHD_create_response_from_buffer(0, nullptr, MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(rsp, "Location", "/gui/index.html");
		MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_TEMPORARY_REDIRECT, rsp);
		MHD_destroy_response(rsp);
		return ret;
	}

	MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, rsp);
	MHD_destroy_response(rsp);
	return ret;
}

bool httpd::start_daemon()
{
	d = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION,
		jconf::inst()->GetHttpdPort(), nullptr, nullptr,
		&httpd::req_handler,
		nullptr, MHD_OPTION_END);

	if(d == nullptr)
	{
		printer::inst()->print_str("HTTP Daemon failed to start.");
		return false;
	}

	return true;
}

#endif
