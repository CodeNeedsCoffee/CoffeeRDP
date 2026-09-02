/**
 * coffee-rdp-auth: AAD/AVD login helper for CoffeeRDP.
 *
 * Runs as its own process (GTK4 + webkitgtk6.0) so a WebKit crash can't take
 * down the RDP session, and so the connection manager (GTK4) and this helper
 * (also GTK4) don't collide with the session client, which is SDL3 and
 * cannot share a process with GTK's own main loop. See PLAN.md section 4.
 *
 * Two modes:
 *
 *  coffee-rdp-auth <auth-url> [window-title]
 *    One-shot: prints exactly the authorization code to stdout on success,
 *    exits 0; prints nothing to stdout and a diagnostic to stderr on
 *    failure/cancel, exits non-zero. Used when coffee-rdp-session can't
 *    reach a resident helper (no coffeerdp manager running to own one --
 *    the standalone CLI path this project has been verified against from
 *    Phase 4 onward, see webview_impl.cpp).
 *
 *  coffee-rdp-auth --serve
 *    Resident: stays running, serving RequestToken calls over a private
 *    peer-to-peer GDBus connection at authIpcSocketPath() (PLAN.md Phase
 *    7.5). Holds one long-lived WebKitWebView across requests instead of a
 *    fresh process per connection, so a reconnect while coffeerdp is open
 *    doesn't restart WebKit from scratch. Each request loads hidden first;
 *    the window is only presented if the login doesn't resolve within
 *    kRevealDelayMs, so a silently-honored session cookie never flashes a
 *    window at all. Exits on a Quit call (sent by the connection manager on
 *    a real quit) or SIGTERM (sent by the manager if the process needs to
 *    be reaped without a clean D-Bus round trip).
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
#include <jsc/jsc.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include <coffee-rdp-auth-generated.h>
#include <coffee_auth_ipc.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

/** How long a request is allowed to try resolving silently (an honored
 *  session cookie completing the redirect on its own) before the window is
 *  actually shown to the user. See PLAN.md Phase 7.5: whether this ever
 *  actually fires silently, versus Conditional Access forcing a credential
 *  prompt every time regardless of session continuity, is the open
 *  question real usage will answer -- either way, a login that does need
 *  interaction still appears within this delay, so there's no functional
 *  downside to trying hidden-first. */
constexpr guint kRevealDelayMs = 1500;

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

std::string redirectSchemeOf(const std::string& authUrl)
{
	auto params = parseQuery(queryOf(authUrl));
	auto it = params.find("redirect_uri");
	if (it == params.end())
		return "";
	return schemeOf(urlDecode(it->second));
}

struct OpVaultItem
{
	std::string vault;
	std::string item;
};

/** Parses "op://vault/item/field" into vault + item (URL-decoded, same
 *  decoder already used above for query params -- op:// references use
 *  the same percent-encoding for a vault/item name containing spaces or
 *  other special characters). The field segment is discarded: this is only
 *  ever used to derive vault+item for `op item get ... --otp`, which
 *  resolves an item's own primary one-time password regardless of which
 *  specific field a password reference happened to point at (see
 *  resolveOtpCode()). std::nullopt for anything not shaped like
 *  op://vault/item/... at all. */
std::optional<OpVaultItem> parseOpItemRef(const std::string& ref)
{
	constexpr const char* kPrefix = "op://";
	if (ref.rfind(kPrefix, 0) != 0)
		return std::nullopt;

	const auto rest = ref.substr(std::strlen(kPrefix));
	const auto firstSlash = rest.find('/');
	if (firstSlash == std::string::npos)
		return std::nullopt;
	const auto secondSlash = rest.find('/', firstSlash + 1);
	if (secondSlash == std::string::npos)
		return std::nullopt;

	OpVaultItem out;
	out.vault = urlDecode(rest.substr(0, firstSlash));
	out.item = urlDecode(rest.substr(firstSlash + 1, secondSlash - firstSlash - 1));
	if (out.vault.empty() || out.item.empty())
		return std::nullopt;
	return out;
}

std::string stateDir()
{
	static std::string dir;
	if (dir.empty())
		dir = std::string(g_get_user_data_dir()) + "/coffeerdp";
	return dir;
}

/* ---- AAD/AVD password autofill (best-effort, both modes). ------------- */

std::string jsStringLiteral(const std::string& s)
{
	std::string out = "\"";
	for (unsigned char c : s)
	{
		switch (c)
		{
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				if (c < 0x20)
				{
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				}
				else
					out += static_cast<char>(c);
		}
	}
	out += "\"";
	return out;
}

/** How many times a fresh OTP code is resolved and tried, per request. A
 *  wrong/stale TOTP code just fails harmlessly (Microsoft re-shows the same
 *  field), unlike a wrong password -- so unlike passwordAttempted (a strict
 *  one-shot, see AutofillState below), this is a bounded retry rather than
 *  a single try. Still capped, so a genuinely broken reference doesn't
 *  hammer `op`/Microsoft forever. */
constexpr int kMaxOtpAttempts = 3;

/** Runs `op item get <item> --vault <vault> --otp`, which resolves an
 *  item's own *primary* one-time password -- the field belonging to the
 *  login itself -- even when the item has several other OTP-type fields
 *  for unrelated services (a real, observed shape: one Login item holding
 *  five separate TOTP secrets, only one of which is "this" login's own).
 *  That's why this takes vault+item rather than a specific field
 *  reference: `op` already knows which one is primary, no guessing needed
 *  here. Empty on any failure (op missing, item locked, no primary OTP on
 *  this item, etc.) -- caller just doesn't fill/click anything, same
 *  fallback contract as the password path.
 *
 *  fork/pipe/execlp/waitpid + close_range(): same shape and same reasoning
 *  as sdl_webview.cpp's resolveOpPassword() and this project's other raw
 *  fork() call sites -- no shell (nothing to quote/escape even though
 *  vault/item come from a user-editable profile field), and inherited fds
 *  closed before exec so `op`'s own desktop-app socket handshake doesn't
 *  start with a table full of descriptors it has no use for. */
std::string resolveOtpCode(const std::string& vault, const std::string& item)
{
	if (vault.empty() || item.empty())
		return "";

	int pipefd[2] = { -1, -1 };
	if (pipe(pipefd) != 0)
		return "";

	pid_t pid = fork();
	if (pid < 0)
	{
		close(pipefd[0]);
		close(pipefd[1]);
		return "";
	}

	if (pid == 0)
	{
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		close_range(3, ~0U, 0);
		execlp("op", "op", "item", "get", item.c_str(), "--vault", vault.c_str(), "--otp",
		      static_cast<char*>(nullptr));
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
		return "";

	while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
		output.pop_back();
	return output;
}

/** All state one AAD/AVD login request's autofill needs, across however
 *  many load-changed events it takes to get through the flow. A single
 *  instance lives for the duration of one request -- a namespace global in
 *  one-shot mode (one request per process), embedded in ServerState in
 *  serve mode (reset per request in startRequest(), since the WebView
 *  itself outlives any individual request there). Passed by pointer as
 *  evaluate_javascript()'s userData, so it has to live at a stable address
 *  for as long as an async JS call might still be in flight -- both homes
 *  satisfy that. */
struct AutofillState
{
	std::string password;
	bool passwordAttempted = false;
	std::string otpVault;
	std::string otpItem;
	int otpAttempts = 0;
	/* How many extra render-retries (see onAutofillRetryTimeout) have
	 * already been scheduled for the *current* page. Reset implicitly
	 * whenever the whole AutofillState is reset at request start; not
	 * reset per-page, so a page that keeps re-rendering without ever
	 * producing the field still gives up eventually rather than polling
	 * forever across an entire multi-page flow. */
	int renderRetries = 0;
};

/** Microsoft's newer sign-in pages render the actual form client-side,
 *  after the WebView's own "load-changed"/FINISHED event -- confirmed both
 *  live (a probe right at FINISHED saw only hidden scaffolding fields,
 *  page title "Sign in to your account", no visible inputs yet) and by
 *  existing Playwright/Selenium automation for this exact page, which
 *  documents needing an explicit wait for #idTxtBx_SAOTCC_OTC to appear
 *  rather than assuming it's there at page-load. This bounded, short-delay
 *  re-probe is that wait, translated to WebKit's async
 *  evaluate_javascript() rather than a blocking wait. */
constexpr int kMaxRenderRetries = 6; // ~6 * 500ms = 3s of extra patience per page

struct AutofillRetry
{
	WebKitWebView* webview;
	AutofillState* state;
};

void tryAutofill(WebKitWebView* webview, AutofillState* state);

gboolean onAutofillRetryTimeout(gpointer userData)
{
	auto* retry = static_cast<AutofillRetry*>(userData);
	tryAutofill(retry->webview, retry->state);
	delete retry;
	return G_SOURCE_REMOVE;
}

/** Handles the result of tryAutofill()'s probe script. Only flips
 *  `passwordAttempted` once the script itself confirms it found and filled
 *  the password field -- not merely because tryAutofill() ran -- so a page
 *  that hasn't reached that step yet doesn't burn the one attempt before
 *  there was anything to fill. 'awaiting-otp' means the verification-code
 *  field is present and empty but nothing was typed into it yet: resolving
 *  a TOTP code is a synchronous `op` subprocess call, done here rather than
 *  inline in the probe script, then filled via a second, separate
 *  evaluate_javascript() call. */
void onAutofillJsResult(GObject* source, GAsyncResult* result, gpointer userData)
{
	auto* state = static_cast<AutofillState*>(userData);
	GError* error = nullptr;
	JSCValue* value =
	    webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source), result, &error);
	if (error)
	{
		g_printerr("coffee-rdp-auth: autofill script failed to run: %s\n", error->message);
		g_error_free(error);
		return; // the script itself didn't run -- don't burn an attempt
	}

	g_autofree gchar* str =
	    (value && jsc_value_is_string(value)) ? jsc_value_to_string(value) : nullptr;
	g_printerr("coffee-rdp-auth: autofill script result: %s\n", str ? str : "(non-string)");

	if (str && (std::strcmp(str, "filled-password") == 0))
	{
		state->passwordAttempted = true;
	}
	else if (str && (std::strcmp(str, "awaiting-otp") == 0))
	{
		state->otpAttempts++;
		std::string otpCode = resolveOtpCode(state->otpVault, state->otpItem);
		if (!otpCode.empty())
		{
			const std::string fillJs =
			    "(function(){"
			    "var o=document.querySelector('#idTxtBx_SAOTCC_OTC');"
			    "if(o){o.value=" +
			    jsStringLiteral(otpCode) +
			    ";"
			    "o.dispatchEvent(new Event('input',{bubbles:true}));"
			    "var b=document.querySelector('#idSubmit_SAOTCC_Continue');if(b){b.click();}}"
			    "})();";
			webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(source), fillJs.c_str(), -1, nullptr,
			                                    nullptr, nullptr, nullptr, nullptr);
		}
	}
	else
	{
		/* No match yet on anything still worth trying -- most likely
		 * Microsoft's own client-side JS hasn't rendered the actual form
		 * yet (see kMaxRenderRetries' doc comment). Re-probe shortly
		 * rather than giving up after a single check at load-FINISHED. */
		const bool stillPending = (!state->password.empty() && !state->passwordAttempted) ||
		                         (!state->otpVault.empty() && (state->otpAttempts < kMaxOtpAttempts));
		if (stillPending && (state->renderRetries < kMaxRenderRetries))
		{
			state->renderRetries++;
			g_timeout_add(500, onAutofillRetryTimeout,
			             new AutofillRetry{ WEBKIT_WEB_VIEW(source), state });
		}
	}
	if (value)
		g_object_unref(value);
}

/** Best-effort AAD/AVD autofill: password, then (a page or two later) the
 *  MFA verification-code step. Skips entirely if neither is configured for
 *  this request -- ordinary, unmodified interactive login, no separate
 *  fallback path needed beyond just not calling this. Which branches the
 *  generated script even includes depends on current state (already
 *  attempted the password once, already spent all the OTP attempts) rather
 *  than tracking that in JS -- keeps each branch self-contained.
 *
 *  #i0116 (email), #i0118 (password), #idSIButton9 (Next/Sign in),
 *  #idTxtBx_SAOTCC_OTC (verification code), #idSubmit_SAOTCC_Continue
 *  (its Verify button) are Microsoft identity-platform's own long-standing
 *  field/button IDs, the same ones enterprise RPA/browser-automation
 *  tooling has relied on for years. Email is filled server-side already
 *  (see with_login_hint() in sdl_webview.cpp, a login_hint URL param
 *  Microsoft's own page honors natively) -- this only clicks Next if that
 *  already produced a non-empty box, never types into it. Any page
 *  matching none of these (a non-TOTP MFA method, "Stay signed in?", an
 *  account-picker, Conditional Access, or Microsoft simply renaming these
 *  IDs one day) is left completely alone. */
void tryAutofill(WebKitWebView* webview, AutofillState* state)
{
	const bool tryPassword = !state->password.empty() && !state->passwordAttempted;
	const bool tryOtp = !state->otpVault.empty() && (state->otpAttempts < kMaxOtpAttempts);
	if (!tryPassword && !tryOtp)
		return;

	g_printerr("coffee-rdp-auth: running autofill script (password=%d otp=%d) against %s\n",
	          tryPassword, tryOtp, webkit_web_view_get_uri(webview));

	std::string js = "(function(){";
	if (tryPassword)
	{
		js += "var p=document.querySelector('#i0118');"
		     "if(p&&!p.value){p.value=" +
		     jsStringLiteral(state->password) +
		     ";"
		     "p.dispatchEvent(new Event('input',{bubbles:true}));"
		     "var b=document.querySelector('#idSIButton9');if(b){b.click();}"
		     "return 'filled-password';}";
	}
	if (tryOtp)
	{
		js += "var o=document.querySelector('#idTxtBx_SAOTCC_OTC');"
		     "if(o&&!o.value){return 'awaiting-otp';}";
	}
	js += "var e=document.querySelector('#i0116');"
	     "if(e&&e.value){var b2=document.querySelector('#idSIButton9');"
	     "if(b2){b2.click();}}"
	     "return 'no-match';"
	     "})();";

	webkit_web_view_evaluate_javascript(webview, js.c_str(), -1, nullptr, nullptr, nullptr,
	                                    onAutofillJsResult, state);
}

/* ---- One-shot mode: unchanged behavior, run once and exit. ------------ */

namespace oneshot
{

std::string g_code;
int g_exitCode = 1;
GtkApplication* g_app = nullptr;
/* Populated once at startup from COFFEE_RDP_AAD_PASSWORD / COFFEE_RDP_AAD_OP_REF
 * (webview_impl.cpp's runViaSpawnedHelper sets these env vars in the child
 * rather than passing them as argv positionals -- see that file's comment
 * for why). Empty means no autofill for that piece. */
AutofillState g_autofill;

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

void onLoadChanged(WebKitWebView* webview, WebKitLoadEvent event, gpointer)
{
	if (event == WEBKIT_LOAD_FINISHED)
	{
		g_autofill.renderRetries = 0; // fresh render-retry budget for this page
		tryAutofill(webview, &g_autofill);
	}
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

	std::string scheme = redirectSchemeOf(authUrl);
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
	if (std::getenv("COFFEE_RDP_AUTH_DEBUG_INSPECTOR"))
		webkit_settings_set_enable_developer_extras(
		    webkit_web_view_get_settings(WEBKIT_WEB_VIEW(webview)), TRUE);

	WebKitWebContext* webContext = webkit_web_view_get_context(WEBKIT_WEB_VIEW(webview));
	webkit_web_context_register_uri_scheme(webContext, scheme.c_str(), onSchemeRequest, nullptr,
	                                       nullptr);
	g_signal_connect(webview, "load-changed", G_CALLBACK(onLoadChanged), nullptr);

	gtk_window_set_child(GTK_WINDOW(window), webview);
	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webview), authUrl);
	gtk_window_present(GTK_WINDOW(window));
}

int run(const char* url, const char* title)
{
	LaunchArgs launch{ url, title };
	if (const char* pw = std::getenv("COFFEE_RDP_AAD_PASSWORD"))
		g_autofill.password = pw;
	if (const char* ref = std::getenv("COFFEE_RDP_AAD_OP_REF"))
	{
		if (auto parsed = parseOpItemRef(ref))
		{
			g_autofill.otpVault = parsed->vault;
			g_autofill.otpItem = parsed->item;
		}
	}

	GtkApplication* app = gtk_application_new("com.codeneedscoffee.coffeerdp.auth",
	                                          G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(onActivate), &launch);
	g_application_run(G_APPLICATION(app), 0, nullptr);
	g_object_unref(app);

	if (g_exitCode == 0)
		std::printf("%s\n", g_code.c_str());
	return g_exitCode;
}

} // namespace oneshot

/* ---- Serve mode: resident helper, one WebView reused across requests. -
 *
 * Requests are serialized: only one login is ever visually in flight, and
 * a second RequestToken arriving mid-login is queued rather than opening a
 * second window. This matches how a person actually uses CoffeeRDP (one
 * profile connects at a time) and avoids ever needing two WebViews alive at
 * once. */
namespace serve
{

struct PendingRequest
{
	CoffeeRdpAuth1* skeleton;
	GDBusMethodInvocation* invocation;
	std::string url;
	std::string title;
	std::string password;
	std::string opItemRef;
};

struct ServerState
{
	GtkApplication* app = nullptr;
	GDBusServer* server = nullptr;
	std::vector<GDBusConnection*> connections;

	GtkWindow* window = nullptr;
	WebKitWebView* webview = nullptr;
	std::string registeredScheme;

	CoffeeRdpAuth1* currentSkeleton = nullptr;
	GDBusMethodInvocation* currentInvocation = nullptr;
	guint revealTimerId = 0;
	std::deque<PendingRequest> queue;

	/* Current request's autofill state (see AutofillState) -- read by
	 * onLoadChanged(), set in startRequest(), cleared in finishCurrent() so
	 * a resolved secret doesn't linger in memory past the request it was
	 * for. */
	AutofillState autofill;
};

ServerState g_state;

void startNext();

void hideWindow()
{
	if (g_state.window)
		gtk_widget_set_visible(GTK_WIDGET(g_state.window), FALSE);
}

void finishCurrent(int exitCode, const std::string& code)
{
	if (g_state.revealTimerId)
	{
		g_source_remove(g_state.revealTimerId);
		g_state.revealTimerId = 0;
	}

	CoffeeRdpAuth1* skeleton = g_state.currentSkeleton;
	GDBusMethodInvocation* invocation = g_state.currentInvocation;
	g_state.currentSkeleton = nullptr;
	g_state.currentInvocation = nullptr;
	g_state.autofill = AutofillState{};
	hideWindow();

	if (invocation)
	{
		if (exitCode == 0)
			coffee_rdp_auth1_complete_request_token(skeleton, invocation, code.c_str());
		else
			g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
			                                      "coffee-rdp-auth: login failed or was cancelled");
	}

	startNext();
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
		finishCurrent(1, "");
	}
	else
	{
		auto code = params.find("code");
		if (code == params.end())
		{
			g_printerr("coffee-rdp-auth: redirect URI had no 'code' parameter\n");
			finishCurrent(1, "");
		}
		else
		{
			finishCurrent(0, code->second);
		}
	}

	GError* gerr = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	                                   "coffee-rdp-auth: redirect captured, not serving content");
	webkit_uri_scheme_request_finish_error(request, gerr);
	g_error_free(gerr);
}

gboolean onCloseRequest(GtkWindow*, gpointer)
{
	/* Resident helper: closing the window cancels the in-flight login, it
	 * never quits the process (that's Quit(), sent explicitly). */
	if (g_state.currentInvocation)
	{
		g_printerr("coffee-rdp-auth: window closed before login completed\n");
		finishCurrent(1, "");
	}
	hideWindow();
	return TRUE; // don't let GTK destroy the window, it's reused
}

void onLoadChanged(WebKitWebView* webview, WebKitLoadEvent event, gpointer)
{
	if (event != WEBKIT_LOAD_FINISHED)
		return;
	if (!g_state.currentInvocation)
		return; // stray event with no request in flight (e.g. the initial blank load)
	g_state.autofill.renderRetries = 0; // fresh render-retry budget for this page
	tryAutofill(webview, &g_state.autofill);
}

gboolean onRevealTimer(gpointer)
{
	g_state.revealTimerId = 0;
	if (g_state.currentInvocation && g_state.window)
		gtk_window_present(g_state.window);
	return G_SOURCE_REMOVE;
}

void ensureWindowAndWebView()
{
	if (g_state.webview)
		return;

	std::string webkitDir = stateDir() + "/webkit";
	g_mkdir_with_parents(webkitDir.c_str(), 0700);

	WebKitNetworkSession* session =
	    webkit_network_session_new(webkitDir.c_str(), webkitDir.c_str());
	WebKitCookieManager* cookies = webkit_network_session_get_cookie_manager(session);
	std::string cookieFile = webkitDir + "/cookies.sqlite";
	webkit_cookie_manager_set_persistent_storage(cookies, cookieFile.c_str(),
	                                             WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

	GtkWidget* window = gtk_application_window_new(g_state.app);
	gtk_window_set_default_size(GTK_WINDOW(window), 480, 640);
	g_signal_connect(window, "close-request", G_CALLBACK(onCloseRequest), nullptr);

	GtkWidget* webview =
	    GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", session, nullptr));
	if (std::getenv("COFFEE_RDP_AUTH_DEBUG_INSPECTOR"))
		webkit_settings_set_enable_developer_extras(
		    webkit_web_view_get_settings(WEBKIT_WEB_VIEW(webview)), TRUE);
	gtk_window_set_child(GTK_WINDOW(window), webview);
	/* Connected once here rather than per-request: this WebView is reused
	 * across the resident helper's whole lifetime (that's the entire point
	 * of the resident-helper design), so the handler reads whichever
	 * request is current from g_state at fire time instead of being
	 * rewired per RequestToken call. */
	g_signal_connect(webview, "load-changed", G_CALLBACK(onLoadChanged), nullptr);

	g_state.window = GTK_WINDOW(window);
	g_state.webview = WEBKIT_WEB_VIEW(webview);
}

void startRequest(const PendingRequest& req)
{
	ensureWindowAndWebView();
	g_state.autofill = AutofillState{};
	g_state.autofill.password = req.password;
	if (auto parsed = parseOpItemRef(req.opItemRef))
	{
		g_state.autofill.otpVault = parsed->vault;
		g_state.autofill.otpItem = parsed->item;
	}

	std::string scheme = redirectSchemeOf(req.url);
	if (scheme.empty())
	{
		g_printerr("coffee-rdp-auth: could not find redirect_uri in auth URL, aborting\n");
		finishCurrent(1, "");
		return;
	}
	if (scheme != g_state.registeredScheme)
	{
		/* Every CoffeeRDP auth URL uses the same FreeRDP-built redirect_uri,
		 * so this registers once and every later request matches it.
		 * WebKit doesn't support unregistering, so a real scheme change
		 * mid-lifetime (not expected in practice) would need a fresh
		 * WebKitWebContext, not handled here. */
		WebKitWebContext* webContext = webkit_web_view_get_context(g_state.webview);
		webkit_web_context_register_uri_scheme(webContext, scheme.c_str(), onSchemeRequest, nullptr,
		                                       nullptr);
		g_state.registeredScheme = scheme;
	}

	gtk_window_set_title(g_state.window,
	                     !req.title.empty() ? req.title.c_str() : "CoffeeRDP - sign in");
	hideWindow();
	webkit_web_view_load_uri(g_state.webview, req.url.c_str());
	g_state.revealTimerId = g_timeout_add(kRevealDelayMs, onRevealTimer, nullptr);
}

void startNext()
{
	if (g_state.queue.empty())
		return;

	PendingRequest req = std::move(g_state.queue.front());
	g_state.queue.pop_front();
	g_state.currentSkeleton = req.skeleton;
	g_state.currentInvocation = req.invocation;
	startRequest(req);
}

gboolean onHandleRequestToken(CoffeeRdpAuth1* object, GDBusMethodInvocation* invocation,
                              const gchar* url, const gchar* title, const gchar* password,
                              const gchar* op_item_ref, gpointer)
{
	PendingRequest req{ object,       invocation,
		                url ? url : "", title ? title : "",
		                password ? password : "", op_item_ref ? op_item_ref : "" };

	if (g_state.currentInvocation)
	{
		g_state.queue.push_back(std::move(req));
		return TRUE;
	}

	g_state.currentSkeleton = req.skeleton;
	g_state.currentInvocation = req.invocation;
	startRequest(req);
	return TRUE;
}

gboolean onHandleQuit(CoffeeRdpAuth1* object, GDBusMethodInvocation* invocation, gpointer)
{
	coffee_rdp_auth1_complete_quit(object, invocation);
	/* Deferred one iteration so the method reply actually reaches the
	 * caller before the connection/main loop go away. */
	g_idle_add(
	    [](gpointer app) -> gboolean {
		    g_application_quit(G_APPLICATION(app));
		    return G_SOURCE_REMOVE;
	    },
	    g_state.app);
	return TRUE;
}

void onConnectionClosed(GDBusConnection* connection, gboolean, GError*, gpointer)
{
	auto& conns = g_state.connections;
	conns.erase(std::remove(conns.begin(), conns.end(), connection), conns.end());
	g_object_unref(connection);
}

gboolean onNewConnection(GDBusServer*, GDBusConnection* connection, gpointer)
{
	CoffeeRdpAuth1* skeleton = coffee_rdp_auth1_skeleton_new();
	g_signal_connect(skeleton, "handle-request-token", G_CALLBACK(onHandleRequestToken), nullptr);
	g_signal_connect(skeleton, "handle-quit", G_CALLBACK(onHandleQuit), nullptr);

	GError* error = nullptr;
	if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skeleton), connection,
	                                      "/com/codeneedscoffee/coffeerdp/Auth1", &error))
	{
		g_printerr("coffee-rdp-auth: failed to export interface: %s\n", error->message);
		g_error_free(error);
		g_object_unref(skeleton);
		return FALSE;
	}

	/* The skeleton keeps a weak ref to the connection via the export above;
	 * this object_ref is what keeps the connection itself alive for as
	 * long as the peer is connected (GDBusServer's own contract: returning
	 * TRUE means we've claimed ownership). */
	g_object_ref(connection);
	g_state.connections.push_back(connection);
	g_signal_connect(connection, "closed", G_CALLBACK(onConnectionClosed), nullptr);

	return TRUE;
}

gboolean onSigterm(gpointer app)
{
	g_application_quit(G_APPLICATION(app));
	return G_SOURCE_REMOVE;
}

int run()
{
	if (!ensureAuthIpcDir())
	{
		g_printerr("coffee-rdp-auth: could not create runtime directory for %s: %s\n",
		           authIpcSocketPath().c_str(), g_strerror(errno));
		return 1;
	}

	std::string socketPath = authIpcSocketPath();
	unlink(socketPath.c_str()); // stale socket from a crashed prior run

	std::string address = "unix:path=" + socketPath;
	g_autofree gchar* guid = g_dbus_generate_guid();
	GError* error = nullptr;
	g_state.server =
	    g_dbus_server_new_sync(address.c_str(), G_DBUS_SERVER_FLAGS_NONE, guid, nullptr, nullptr,
	                           &error);
	if (!g_state.server)
	{
		g_printerr("coffee-rdp-auth: could not listen on %s: %s\n", socketPath.c_str(),
		           error->message);
		g_error_free(error);
		return 1;
	}
	g_signal_connect(g_state.server, "new-connection", G_CALLBACK(onNewConnection), nullptr);
	g_dbus_server_start(g_state.server);

	g_state.app =
	    gtk_application_new("com.codeneedscoffee.coffeerdp.auth", G_APPLICATION_NON_UNIQUE);
	/* No window exists until the first request, so this hold is what keeps
	 * the main loop running in the meantime -- otherwise g_application_run
	 * would see zero windows and zero holds right after "activate" and
	 * exit immediately. */
	g_application_hold(G_APPLICATION(g_state.app));
	g_unix_signal_add(SIGTERM, onSigterm, g_state.app);
	g_unix_signal_add(SIGINT, onSigterm, g_state.app);

	g_signal_connect(g_state.app, "activate", G_CALLBACK(+[](GtkApplication*, gpointer) {}),
	                 nullptr);
	int rc = g_application_run(G_APPLICATION(g_state.app), 0, nullptr);

	for (auto* connection : g_state.connections)
		g_object_unref(connection);
	g_object_unref(g_state.server);
	g_object_unref(g_state.app);
	unlink(socketPath.c_str());
	return rc;
}

} // namespace serve

} // namespace

int main(int argc, char** argv)
{
	/* The cookie jar holds live Entra session cookies -- credential-grade
	 * material (PLAN.md section 2.3). A restrictive umask, set before any
	 * file gets created, is what guarantees WebKit's own internally-created
	 * cookies.sqlite ends up 0600 rather than whatever the ambient umask
	 * would otherwise leave it at (typically 0644, world-readable). Same
	 * reasoning covers the resident helper's IPC socket. */
	umask(0077);
	g_mkdir_with_parents(stateDir().c_str(), 0700);

	if (argc == 2 && std::strcmp(argv[1], "--serve") == 0)
		return serve::run();

	if (argc != 2 && argc != 3)
	{
		g_printerr("usage: %s <auth-url> [window-title]\n       %s --serve\n",
		          argc > 0 ? argv[0] : "coffee-rdp-auth", argc > 0 ? argv[0] : "coffee-rdp-auth");
		return 2;
	}

	return oneshot::run(argv[1], argc == 3 ? argv[2] : nullptr);
}
