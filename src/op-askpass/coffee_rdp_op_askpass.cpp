/**
 * coffee-rdp-op-askpass: FREERDP_ASKPASS helper backed by the 1Password CLI.
 *
 * FreeRDP's own askpass caller (freerdp_passphrase_read_askpass,
 * libfreerdp/utils/passphrase.c) runs $FREERDP_ASKPASS as
 * `<FREERDP_ASKPASS> '<prompt>'` via popen() and reads the password back
 * from its stdout. The prompt argument is deliberately ignored here: the
 * secret's actual location is a profile's own 1Password reference, passed
 * separately as COFFEE_RDP_OP_REF (coffee_rdp_manager.cpp's
 * connectToProfile()) rather than folded into the FREERDP_ASKPASS command
 * string itself -- an env var isn't shell-parsed, so this sidesteps having
 * to quote/escape an otherwise user-supplied op:// reference into a string
 * that's about to be handed to popen().
 *
 * execlp() rather than fork+exec+capture: this process has nothing else to
 * do, so replacing it entirely with `op` and letting its own stdout/stderr/
 * exit code flow straight through is simplest and correct.
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

int main()
{
	const char* ref = std::getenv("COFFEE_RDP_OP_REF");
	if (!ref || !*ref)
	{
		std::fprintf(stderr, "coffee-rdp-op-askpass: COFFEE_RDP_OP_REF is not set\n");
		return 1;
	}

	/* FreeRDP's popen() call (see this file's own doc comment above) forks
	 * from inside coffee-rdp-session, which by connect time routinely holds
	 * several hundred open fds (GPU sync objects, the Wayland socket, none
	 * O_CLOEXEC -- confirmed via /proc/<pid>/fd on a live session) that
	 * this process would otherwise inherit and hand straight to `op`.
	 * Closing them before exec keeps `op`'s own desktop-app socket
	 * handshake from starting with several hundred unrelated descriptors
	 * already in its table -- a sibling raw fork() in sdl_webview.cpp's
	 * resolveOpPassword() found this the hard way (intermittent
	 * "connection reset" / "reading frame length: EOF" failures talking to
	 * the 1Password desktop app, with nothing on its side to explain them:
	 * a client-side transport issue, not a server-side rejection). */
	close_range(3, ~0U, 0);

	execlp("op", "op", "read", "--no-newline", ref, static_cast<char*>(nullptr));

	// Only reached if exec itself failed (e.g. op isn't installed).
	std::fprintf(stderr, "coffee-rdp-op-askpass: could not run 'op': %s\n", std::strerror(errno));
	return 127;
}
