/**
 * coffee-rdp-auth: standalone AAD/AVD login helper for CoffeeRDP.
 *
 * Runs as its own process (GTK4 + webkitgtk6.0) so a WebKit crash can't take
 * down the RDP session, and so the connection manager (GTK4) and this helper
 * (also GTK4) don't collide with the session client, which is SDL3 and
 * cannot share a process with GTK's own main loop. See PLAN.md section 4.
 *
 * Usage: coffee-rdp-auth <auth-url>
 * On success: prints exactly the authorization code to stdout, exits 0.
 * On failure/cancel: prints nothing to stdout, a diagnostic to stderr,
 * exits non-zero. All diagnostics go to stderr so stdout is always safe for
 * the caller to read as "the code, or nothing".
 *
 * Persistence: cookies survive across runs via a disk-backed
 * WebKitNetworkSession (confirmed working -- PLAN.md section 2.3a, the
 * Phase 0a spike). Auth-code capture uses webkit_web_context_register_uri_scheme()
 * to claim the redirect URI's scheme before navigating there -- WebKit
 * doesn't route unregistered custom schemes (e.g. "ms-appx-web://") through
 * decide-policy at all, it just fails the load with "The URL can't be
 * shown" (PLAN.md section 2.3a's "secondary, unrelated finding", from the
 * same spike). Registering the scheme is what the spike's decide-policy
 * approach was missing.
 */

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <glib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include <sys/stat.h>

namespace
{

std::string g_code;
int g_exitCode = 1;
GtkApplication* g_app = nullptr;

std::string urlDecode(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] == '%' && i + 2 < s.size())
		{
			auto hex = s.substr(i + 1, 2);
			out += static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
			i += 2;
		}
		else if (s[i] == '+')
			out += ' ';
		else
			out += s[i];
	}
	return out;
}

std::unordered_map<std::string, std::string> parseQuery(const std::string& query)
{
	std::unordered_map<std::string, std::string> out;
	size_t start = 0;
	while (start < query.size())
	{
		size_t amp = query.find('&', start);
		std::string pair =
		    (amp == std::string::npos) ? query.substr(start) : query.substr(start, amp - start);
		auto eq = pair.find('=');
		if (eq != std::string::npos)
			out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
		if (amp == std::string::npos)
			break;
		start = amp + 1;
	}
	return out;
}

/** Everything after "scheme://" in a URL, i.e. just the scheme name. */
std::string schemeOf(const std::string& url)
{
	auto pos = url.find("://");
	return pos == std::string::npos ? "" : url.substr(0, pos);
}

std::string queryOf(const std::string& url)
{
	auto pos = url.find('?');
	return pos == std::string::npos ? "" : url.substr(pos + 1);
}

std::string stateDir()
{
	static std::string dir;
	if (dir.empty())
		dir = std::string(g_get_user_data_dir()) + "/coffeerdp";
	return dir;
}

void finish(int exitCode, const std::string& code)
{
	g_exitCode = exitCode;
	g_code = code;
	if (g_app)
		g_application_quit(G_APPLICATION(g_app));
}

void onSchemeRequest(WebKitURISchemeRequest* request, gpointer)
{
	const char* uri = webkit_uri_scheme_request_get_uri(request);
	std::string query = queryOf(uri ? uri : "");
	auto params = parseQuery(query);

	auto err = params.find("error");
	if (err != params.end())
	{
		auto sub = params.find("error_subcode");
		g_printerr("coffee-rdp-auth: server returned error: %s%s%s\n", err->second.c_str(),
		           sub != params.end() ? ": " : "", sub != params.end() ? sub->second.c_str() : "");
		finish(1, "");
	}
	else
	{
		auto code = params.find("code");
		if (code == params.end())
		{
			g_printerr("coffee-rdp-auth: redirect URI had no 'code' parameter\n");
			finish(1, "");
		}
		else
		{
			finish(0, code->second);
		}
	}

	GError* gerr = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	                                   "coffee-rdp-auth: redirect captured, not serving content");
	webkit_uri_scheme_request_finish_error(request, gerr);
	g_error_free(gerr);
}

gboolean onCloseRequest(GtkWindow*, gpointer)
{
	if (g_code.empty())
	{
		g_printerr("coffee-rdp-auth: window closed before login completed\n");
		finish(1, "");
	}
	return FALSE; // allow the close to proceed
}

struct LaunchArgs
{
	const char* url;
	const char* title;
};

void onActivate(GtkApplication* app, gpointer userData)
{
	auto* launch = static_cast<LaunchArgs*>(userData);
	const char* authUrl = launch->url;
	g_app = app;

	std::string redirectUri = [&] {
		auto params = parseQuery(queryOf(authUrl));
		auto it = params.find("redirect_uri");
		return it != params.end() ? urlDecode(it->second) : std::string();
	}();
	std::string scheme = schemeOf(redirectUri);

	if (scheme.empty())
	{
		g_printerr("coffee-rdp-auth: could not find redirect_uri in auth URL, aborting\n");
		finish(1, "");
		return;
	}

	std::string webkitDir = stateDir() + "/webkit";
	g_mkdir_with_parents(webkitDir.c_str(), 0700);

	WebKitNetworkSession* session =
	    webkit_network_session_new(webkitDir.c_str(), webkitDir.c_str());
	WebKitCookieManager* cookies = webkit_network_session_get_cookie_manager(session);
	std::string cookieFile = webkitDir + "/cookies.sqlite";
	webkit_cookie_manager_set_persistent_storage(cookies, cookieFile.c_str(),
	                                             WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

	GtkWidget* window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), (launch->title && *launch->title) ? launch->title
	                                                                          : "CoffeeRDP - sign in");
	gtk_window_set_default_size(GTK_WINDOW(window), 480, 640);
	g_signal_connect(window, "close-request", G_CALLBACK(onCloseRequest), nullptr);

	GtkWidget* webview =
	    GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", session, nullptr));

	WebKitWebContext* webContext = webkit_web_view_get_context(WEBKIT_WEB_VIEW(webview));
	webkit_web_context_register_uri_scheme(webContext, scheme.c_str(), onSchemeRequest, nullptr,
	                                       nullptr);

	gtk_window_set_child(GTK_WINDOW(window), webview);
	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webview), authUrl);
	gtk_window_present(GTK_WINDOW(window));
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 2 && argc != 3)
	{
		g_printerr("usage: %s <auth-url> [window-title]\n", argc > 0 ? argv[0] : "coffee-rdp-auth");
		return 2;
	}

	/* The cookie jar holds live Entra session cookies -- credential-grade
	 * material (PLAN.md section 2.3). A restrictive umask, set before any
	 * file gets created, is what guarantees WebKit's own internally-created
	 * cookies.sqlite ends up 0600 rather than whatever the ambient umask
	 * would otherwise leave it at (typically 0644, world-readable). */
	umask(0077);

	g_mkdir_with_parents(stateDir().c_str(), 0700);

	LaunchArgs launch{ argv[1], argc == 3 ? argv[2] : nullptr };

	GtkApplication* app =
	    gtk_application_new("com.coffeerdp.auth", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(onActivate), &launch);
	g_application_run(G_APPLICATION(app), 0, nullptr);
	g_object_unref(app);

	if (g_exitCode == 0)
		std::printf("%s\n", g_code.c_str());
	return g_exitCode;
}
