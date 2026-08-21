#pragma once

#include <string>

/**
 * Shared paths and helpers for the resident coffee-rdp-auth <-> coffee-rdp-
 * session peer-to-peer GDBus link (PLAN.md Phase 7.5). Both sides need to
 * agree on exactly the same socket path without either one hardcoding it
 * twice.
 */

/** $XDG_RUNTIME_DIR/coffeerdp/auth.sock: a per-user, per-boot runtime
 *  endpoint, not persistent state, so it belongs under XDG_RUNTIME_DIR
 *  (already 0700, tmpfs-backed, gone on logout) rather than
 *  ~/.local/share/coffeerdp, which is reserved for the durable cookie jar
 *  (see PLAN.md section 2.3). */
std::string authIpcSocketPath();

/** Creates authIpcSocketPath()'s parent directory with 0700 permissions if
 *  it doesn't already exist. Returns false (and leaves errno set) on
 *  failure. */
bool ensureAuthIpcDir();
