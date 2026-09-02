/**
 * CoffeeRDP: AAD login popup, either via a resident helper or spawned fresh
 *
 * This used to run akallabeth/webview in-process (GTK3, via CMake
 * FetchContent). Replaced per PLAN.md section 4: GTK3 and GTK4 can't share a
 * process, and a WebKit crash here used to take the whole RDP session down
 * with it. coffee-rdp-auth (GTK4 + webkitgtk6.0, its own binary, built
 * alongside this one) now does the actual login UI and persistent cookie
 * jar. Same public interface (webview_impl_run()) as before, so
 * sdl_webview.cpp -- which already correctly builds the AAD/AVD URLs via
 * FreeRDP's own freerdp_client_get_aad_url() -- needed no changes at all.
 *
 * PLAN.md Phase 7.5 added a second path: if coffeerdp (the connection
 * manager) has a resident `coffee-rdp-auth --serve` running, this talks to
 * it over a private peer-to-peer GDBus connection instead of spawning a
 * fresh process, so a reconnect while coffeerdp is open reuses the same
 * long-lived WebView rather than restarting WebKit from scratch. If no
 * resident helper is reachable (no manager running -- the standalone
 * `coffee-rdp-session` CLI path this whole project has been verified
 * against since Phase 4), this falls straight back to the original
 * fork/exec one-shot path, unchanged.
 */

#include "webview_impl.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>
#include <sys/wait.h>

#include <gio/gio.h>
#include <coffee-rdp-auth-generated.h>
#include <coffee_auth_ipc.hpp>

#include <freerdp/log.h>

#define TAG FREERDP_TAG("client.session.aad")

namespace
{

/** Tries the resident coffee-rdp-auth --serve helper over its private P2P
 *  GDBus socket. Returns false only when no such helper is reachable at
 *  all (no socket, connection refused): the caller should then fall back
 *  to spawning a fresh one-shot process. If the connection succeeds but
 *  the login itself fails/is cancelled, that's a real result, not a
 *  fallback case: returns false with code left empty, exactly like the
 *  one-shot path's own failure contract. */
bool runViaResidentHelper(const std::string& title, const std::string& url,
                          const std::string& password, const std::string& opItemRef,
                          std::string& code, bool& helperReachable)
{
	helperReachable = false;
	std::string address = "unix:path=" + authIpcSocketPath();

	GError* error = nullptr;
	GDBusConnection* connection = g_dbus_connection_new_for_address_sync(
	    address.c_str(), G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, nullptr, nullptr, &error);
	if (!connection)
	{
		// No resident helper listening -- not an error, just "not present".
		g_error_free(error);
		return false;
	}
	helperReachable = true;

	CoffeeRdpAuth1* proxy = coffee_rdp_auth1_proxy_new_sync(
	    connection, G_DBUS_PROXY_FLAGS_NONE, nullptr, "/com/codeneedscoffee/coffeerdp/Auth1",
	    nullptr, &error);
	if (!proxy)
	{
		WLog_WARN(TAG, "webview_impl_run: could not create proxy to resident auth helper: %s",
		          error->message);
		g_error_free(error);
		g_object_unref(connection);
		return false;
	}
	// An interactive login (MFA, account picker, ...) can take arbitrarily
	// long; the proxy's 25s default timeout is for ordinary method calls,
	// not this one.
	g_dbus_proxy_set_default_timeout(G_DBUS_PROXY(proxy), G_MAXINT);

	gchar* outCode = nullptr;
	gboolean rc = coffee_rdp_auth1_call_request_token_sync(
	    proxy, url.c_str(), title.c_str(), password.c_str(), opItemRef.c_str(), &outCode, nullptr,
	    &error);
	g_object_unref(proxy);
	g_object_unref(connection);

	if (!rc)
	{
		WLog_WARN(TAG, "webview_impl_run: resident auth helper reported failure: %s",
		          error->message);
		g_error_free(error);
		return false;
	}

	code = outCode ? outCode : "";
	g_free(outCode);
	return !code.empty();
}

bool isExecutable(const std::string& path)
{
	return !path.empty() && (access(path.c_str(), X_OK) == 0);
}

/** Same directory as this running binary -- the layout `ninja install`
 *  produces, coffee-rdp-session and coffee-rdp-auth side by side. */
std::string selfExeDir()
{
	char buf[4096];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len <= 0)
		return "";
	buf[len] = '\0';
	std::string path(buf);
	auto pos = path.find_last_of('/');
	return (pos == std::string::npos) ? "" : path.substr(0, pos);
}

/** Locates coffee-rdp-auth: next to this binary (installed layout), then
 *  the build-tree sibling directory (uninstalled dev builds, so `ninja` is
 *  enough to test without `ninja install` first), then finally just the bare
 *  name for execvp()'s own PATH search. execlp() below handles all three
 *  uniformly -- a path containing '/' is exec'd directly, a bare name is
 *  PATH-searched. */
std::string findAuthHelper()
{
	auto dir = selfExeDir();
	if (!dir.empty())
	{
		std::string candidate = dir + "/coffee-rdp-auth";
		if (isExecutable(candidate))
			return candidate;
	}

#if defined(COFFEE_RDP_AUTH_BUILD_PATH)
	if (isExecutable(COFFEE_RDP_AUTH_BUILD_PATH))
		return COFFEE_RDP_AUTH_BUILD_PATH;
#endif

	return "coffee-rdp-auth";
}

bool runViaSpawnedHelper(const std::string& title, const std::string& url,
                         const std::string& password, const std::string& opItemRef,
                         std::string& code)
{
	auto helper = findAuthHelper();

	int pipefd[2] = { -1, -1 };
	if (pipe(pipefd) != 0)
	{
		WLog_ERR(TAG, "webview_impl_run: pipe() failed: %s", strerror(errno));
		return false;
	}

	pid_t pid = fork();
	if (pid < 0)
	{
		WLog_ERR(TAG, "webview_impl_run: fork() failed: %s", strerror(errno));
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}

	if (pid == 0)
	{
		// Child: stdout -> pipe, then exec. _exit(127) on exec failure is
		// the parent's signal that the helper binary itself is missing,
		// distinct from the helper running and failing on its own.
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		/* Env var, not argv: /proc/PID/cmdline is world-readable on a stock
		 * install (no hidepid=2), /proc/PID/environ isn't -- same reasoning
		 * as COFFEE_RDP_OP_REF elsewhere. setenv() here only affects this
		 * forked child, never the parent. */
		if (!password.empty())
			setenv("COFFEE_RDP_AAD_PASSWORD", password.c_str(), 1);
		if (!opItemRef.empty())
			setenv("COFFEE_RDP_AAD_OP_REF", opItemRef.c_str(), 1);
		/* coffee-rdp-session routinely holds several hundred open fds by
		 * connect time (GPU sync objects, the Wayland socket, etc., none
		 * O_CLOEXEC -- confirmed via /proc/<pid>/fd on a live session);
		 * close them before exec so coffee-rdp-auth (and, transitively,
		 * WebKit) starts with a clean table instead of several hundred
		 * inherited handles it has no use for. See sdl_webview.cpp's
		 * resolveOpPassword() for the same fix and the reasoning that led
		 * to it (a sibling raw fork() found this the hard way). */
		close_range(3, ~0U, 0);
		execlp(helper.c_str(), helper.c_str(), url.c_str(), title.c_str(),
		      static_cast<char*>(nullptr));
		_exit(127);
	}

	// Parent
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
		if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
			WLog_WARN(TAG, "webview_impl_run: could not run '%s' -- is coffee-rdp-auth installed?",
			          helper.c_str());
		else if (WIFSIGNALED(status))
			WLog_WARN(TAG, "webview_impl_run: coffee-rdp-auth killed by signal %d",
			          WTERMSIG(status));
		else
			WLog_WARN(TAG, "webview_impl_run: coffee-rdp-auth exited with status %d",
			          WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return false;
	}

	while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
		output.pop_back();

	if (output.empty())
		return false;

	code = output;
	return true;
}

} // namespace

bool webview_impl_run(const std::string& title, const std::string& url, const std::string& password,
                      const std::string& opItemRef, std::string& code)
{
	bool helperReachable = false;
	if (runViaResidentHelper(title, url, password, opItemRef, code, helperReachable))
		return true;
	if (helperReachable)
	{
		// The resident helper answered but the login itself failed or was
		// cancelled: that's a real result, spawning a second, competing
		// attempt would be wrong.
		return false;
	}

	return runViaSpawnedHelper(title, url, password, opItemRef, code);
}
