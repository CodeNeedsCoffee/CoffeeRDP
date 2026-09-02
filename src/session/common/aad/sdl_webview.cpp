/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Popup browser for AAD authentication
 *
 * Copyright 2023 Isaac Klein <fifthdegree@protonmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>
#include <sstream>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <unistd.h>
#include <sys/wait.h>

#include <winpr/string.h>
#include <freerdp/log.h>
#include <freerdp/utils/aad.h>

#include "sdl_webview.hpp"
#include "webview_impl.hpp"

#define TAG CLIENT_TAG("SDL.webview")

/** Resolves the AAD/AVD password via `op read` on COFFEE_RDP_OP_REF (the
 *  same env var coffee_rdp_manager.cpp's connectToProfile() already sets
 *  whenever a profile has a 1Password reference, unconditionally of
 *  aad_auth -- the NLA/FREERDP_ASKPASS path just happens to be the other
 *  consumer of it). Empty on any failure (no reference configured, `op`
 *  missing, item locked, etc.) -- callers pass that straight through to
 *  webview_impl_run(), which treats an empty password as "no autofill,
 *  ordinary interactive login" with no separate fallback path needed.
 *
 *  fork/pipe/execlp/waitpid rather than popen(): same reasoning as
 *  coffee-rdp-op-askpass and webview_impl.cpp's runViaSpawnedHelper --
 *  execing `op` directly means the reference never passes through a shell,
 *  so there's nothing to quote/escape even though it came from a
 *  user-editable profile field. */
static std::string resolveOpPassword()
{
	const char* ref = std::getenv("COFFEE_RDP_OP_REF");
	if (!ref || !(*ref))
	{
		WLog_WARN(TAG, "resolveOpPassword: COFFEE_RDP_OP_REF not set, no autofill");
		return "";
	}

	int pipefd[2] = { -1, -1 };
	if (pipe(pipefd) != 0)
	{
		WLog_WARN(TAG, "resolveOpPassword: pipe() failed: %s", strerror(errno));
		return "";
	}

	pid_t pid = fork();
	if (pid < 0)
	{
		WLog_WARN(TAG, "resolveOpPassword: fork() failed: %s", strerror(errno));
		close(pipefd[0]);
		close(pipefd[1]);
		return "";
	}

	if (pid == 0)
	{
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		/* This process (coffee-rdp-session) routinely holds several hundred
		 * open fds by the time a connection is live -- GPU sync objects,
		 * the Wayland socket, NVIDIA device handles, none of them
		 * O_CLOEXEC, confirmed via /proc/<pid>/fd on a live session. `op`
		 * inheriting all of that corrupted its own desktop-app handshake in
		 * testing ("connection reset" / "reading frame length: EOF" from
		 * its own client library, no matching rejection in 1Password's own
		 * log -- a client-side transport issue, not a server-side one).
		 * GLib's g_spawn_* calls elsewhere in this codebase close
		 * inherited fds by default; a raw fork() doesn't, so it's done
		 * explicitly here. */
		close_range(3, ~0U, 0);
		execlp("op", "op", "read", "--no-newline", ref, static_cast<char*>(nullptr));
		_exit(127);
	}

	close(pipefd[1]);
	std::string output;
	char buf[4096];
	ssize_t n = 0;
	while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
		output.append(buf, static_cast<size_t>(n));
	close(pipefd[0]);

	int status = 0;
	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		WLog_WARN(TAG,
		         "resolveOpPassword: 'op read' failed (exited=%d status=%d signaled=%d "
		         "sig=%d), outputLen=%zu -- falling back to interactive login",
		         WIFEXITED(status), WIFEXITED(status) ? WEXITSTATUS(status) : -1,
		         WIFSIGNALED(status), WIFSIGNALED(status) ? WTERMSIG(status) : -1, output.size());
		return "";
	}

	WLog_INFO(TAG, "resolveOpPassword: resolved a password (length %zu)", output.size());
	return output;
}

/** Appends a `login_hint=<username>` query param -- a standard Microsoft
 *  identity-platform authorize-endpoint parameter -- to the AAD/AVD auth
 *  URL, so the WebView's sign-in page arrives with the email/UPN field
 *  pre-filled instead of blank. Username comes from whatever CoffeeRDP
 *  already put in FreeRDP's own settings (profile's `/u:`, see
 *  coffee_profiles.cpp's sessionArgs()) -- this reuses that rather than
 *  needing its own separate field. No-op (returns the url unchanged) when
 *  no username is set or it doesn't URL-encode. */
static std::string with_login_hint(std::string url, freerdp* instance)
{
	auto username = freerdp_settings_get_string(instance->context->settings, FreeRDP_Username);
	if (!username || !(*username))
		return url;

	char* encoded = winpr_str_url_encode(username, strlen(username));
	if (!encoded)
		return url;

	url += (url.find('?') == std::string::npos) ? '?' : '&';
	url += "login_hint=";
	url += encoded;
	free(encoded);
	return url;
}

static std::string from_settings(const rdpSettings* settings, FreeRDP_Settings_Keys_String id)
{
	auto val = freerdp_settings_get_string(settings, id);
	if (!val)
	{
		WLog_WARN(TAG, "Settings key %s is nullptr", freerdp_settings_get_name_for_key(id));
		return "";
	}
	return val;
}

static std::string from_aad_wellknown(rdpContext* context, AAD_WELLKNOWN_VALUES which)
{
	auto val = freerdp_utils_aad_get_wellknown_string(context, which);

	if (!val)
	{
		WLog_WARN(TAG, "[wellknown] key %s is nullptr",
		          freerdp_utils_aad_wellknwon_value_name(which));
		return "";
	}
	return val;
}

static BOOL sdl_webview_get_rdsaad_access_token(freerdp* instance, const char* scope,
                                                const char* req_cnf, char** token)
{
	WINPR_ASSERT(instance);
	WINPR_ASSERT(scope);
	WINPR_ASSERT(req_cnf);
	WINPR_ASSERT(token);

	WINPR_UNUSED(instance);

	auto context = instance->context;
	WINPR_UNUSED(context);

	auto settings = context->settings;
	WINPR_ASSERT(settings);

	std::shared_ptr<char> request(freerdp_client_get_aad_url((rdpClientContext*)instance->context,
	                                                         FREERDP_CLIENT_AAD_AUTH_REQUEST,
	                                                         scope),
	                              free);
	const std::string title = "RDP WebView - AAD Auth";
	std::string code;
	const char* opRef = std::getenv("COFFEE_RDP_OP_REF");
	auto rc = webview_impl_run(title, with_login_hint(request.get(), instance), resolveOpPassword(),
	                           opRef ? opRef : "", code);
	if (!rc || code.empty())
		return FALSE;

	std::shared_ptr<char> token_request(
	    freerdp_client_get_aad_url((rdpClientContext*)instance->context,
	                               FREERDP_CLIENT_AAD_TOKEN_REQUEST, scope, code.c_str(), req_cnf),
	    free);
	return client_common_get_access_token(instance, token_request.get(), token);
}

static BOOL sdl_webview_get_avd_access_token(freerdp* instance, char** token)
{
	WINPR_ASSERT(token);
	WINPR_ASSERT(instance);
	WINPR_ASSERT(instance->context);

	std::shared_ptr<char> request(freerdp_client_get_aad_url((rdpClientContext*)instance->context,
	                                                         FREERDP_CLIENT_AAD_AVD_AUTH_REQUEST),
	                              free);

	const std::string title = "RDP WebView - AVD Auth";
	std::string code;
	const char* opRef = std::getenv("COFFEE_RDP_OP_REF");
	auto rc = webview_impl_run(title, with_login_hint(request.get(), instance), resolveOpPassword(),
	                           opRef ? opRef : "", code);
	if (!rc || code.empty())
		return FALSE;

	std::shared_ptr<char> token_request(
	    freerdp_client_get_aad_url((rdpClientContext*)instance->context,
	                               FREERDP_CLIENT_AAD_AVD_TOKEN_REQUEST, code.c_str()),
	    free);
	return client_common_get_access_token(instance, token_request.get(), token);
}

BOOL sdl_webview_get_access_token(freerdp* instance, AccessTokenType tokenType, char** token,
                                  size_t count, ...)
{
	WINPR_ASSERT(instance);
	WINPR_ASSERT(token);
	switch (tokenType)
	{
		case ACCESS_TOKEN_TYPE_AAD:
		{
			if (count < 2)
			{
				WLog_ERR(TAG,
				         "ACCESS_TOKEN_TYPE_AAD expected 2 additional arguments, but got %" PRIuz
				         ", aborting",
				         count);
				return FALSE;
			}
			else if (count > 2)
				WLog_WARN(TAG,
				          "ACCESS_TOKEN_TYPE_AAD expected 2 additional arguments, but got %" PRIuz
				          ", ignoring",
				          count);
			va_list ap = {};
			va_start(ap, count);
			const char* scope = va_arg(ap, const char*);
			const char* req_cnf = va_arg(ap, const char*);
			const BOOL rc = sdl_webview_get_rdsaad_access_token(instance, scope, req_cnf, token);
			va_end(ap);
			return rc;
		}
		case ACCESS_TOKEN_TYPE_AVD:
			if (count != 0)
				WLog_WARN(TAG,
				          "ACCESS_TOKEN_TYPE_AVD expected 0 additional arguments, but got %" PRIuz
				          ", ignoring",
				          count);
			return sdl_webview_get_avd_access_token(instance, token);
		default:
			WLog_ERR(TAG, "Unexpected value for AccessTokenType [%" PRIu32 "], aborting",
			         tokenType);
			return FALSE;
	}
}
