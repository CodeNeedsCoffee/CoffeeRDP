/**
 * CoffeeRDP: AAD login popup, spawned as a separate process
 *
 * This used to run akallabeth/webview in-process (GTK3, via CMake
 * FetchContent). Replaced per PLAN.md section 4: GTK3 and GTK4 can't share a
 * process, and a WebKit crash here used to take the whole RDP session down
 * with it. coffee-rdp-auth (GTK4 + webkitgtk6.0, its own binary, built
 * alongside this one) now does the actual login UI and persistent cookie
 * jar; this file just spawns it and reads the authorization code back over
 * a pipe. Same public interface (webview_impl_run()) as before, so
 * sdl_webview.cpp -- which already correctly builds the AAD/AVD URLs via
 * FreeRDP's own freerdp_client_get_aad_url() -- needed no changes at all.
 */

#include "webview_impl.hpp"

#include <cerrno>
#include <cstring>
#include <string>

#include <unistd.h>
#include <sys/wait.h>

#include <freerdp/log.h>

#define TAG FREERDP_TAG("client.session.aad")

namespace
{
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
} // namespace

bool webview_impl_run(const std::string& title, const std::string& url, std::string& code)
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
