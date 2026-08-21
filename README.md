# CoffeeRDP

![CoffeeRDP](resources/icon/coffeerdp-wordmark.png)

A minimal RDP client for Linux, built on [FreeRDP](https://www.freerdp.com/)'s
SDL3 client, with a GTK4 connection manager on top.

Goals:

- Wayland-first, with an automatic X11 fallback when Wayland can't deliver
- Persistent Entra ID (AAD) login instead of a full MFA challenge every
  connection, reusing one signed-in session for as long as coffeerdp is
  open (optionally in the background past closing its window)
- An idle keep-alive so sessions don't disconnect while you're away
- A compact in-session control bar: pin, an overflow menu, minimize,
  fullscreen/restore, and disconnect, with keyboard capture, Ctrl+Alt+Del,
  Send Super, keep-alive, and a local-shortcuts toggle in the dropdown
- Selectable connection quality presets
- Per-profile options for hosts that need them: skip TLS certificate
  checks (Entra/AVD hosts rotate their certificate roughly daily) and
  disable the client's own Right-Shift shortcuts
- A connection manager with saved profiles, in-place `.rdp` editing, and
  drag-and-drop import that keeps a profile linked to the file it came from

## Status

Under active development. Working today: the session client, quality
presets, idle keep-alive, the in-session control bar, persistent Entra ID
login (including AAD/RDSAAD-specific negotiation), and a GTK4 connection
manager that saves profiles and can import, export, and edit `.rdp` files
in place. TLS negotiation, certificate handling, and the AAD sign-in
prompt have all been confirmed against a real host by invoking the
session client directly with the same arguments the manager builds; no
one has yet clicked Connect in the manager itself and watched a session
come up start to finish.

## Installing

Packaged for Fedora via [Copr](https://copr.fedorainfracloud.org/):

```sh
sudo dnf copr enable codeneedscoffee/coffeerdp
sudo dnf install coffeerdp
```

That pulls in `coffee-rdp-session` and `coffee-rdp-auth` alongside the
`coffeerdp` connection manager, and installs a GNOME Software / desktop
launcher entry. See PACKAGING.md for more.

## Building

Requires FreeRDP 3's development packages (pkg-config modules `freerdp3`,
`freerdp-client3`, `winpr3`; on Fedora, the `freerdp-devel` and
`libwinpr-devel` RPMs, despite the version-3 naming only showing up in the
pkg-config files themselves), SDL3 and SDL3_ttf, GTK4, libadwaita,
webkitgtk 6.0, glib2 (gio/gdbus-codegen, already pulled in transitively by
GTK4 but named explicitly), and libportal-gtk4 (the XDG Background portal
client, for running past the manager window closing).

```sh
cmake -S . -B build -GNinja
ninja -C build
```

## Layout

```
src/session/   SDL3 session client (forked from FreeRDP's client/SDL/SDL3
               and client/SDL/common), built at build/src/session
src/auth/      GTK4/WebKit AAD login helper, either run one-shot per
               connection or resident (`--serve`, owned by coffeerdp) and
               reused across them, built at build/src/auth
src/auth-ipc/  GDBus interface shared between coffee-rdp-session and the
               resident coffee-rdp-auth --serve helper (peer-to-peer, not
               the session bus), built at build/src/auth-ipc
src/manager/   GTK4 connection manager, built at build/src/manager
resources/     Bundled assets: fonts and the app icon
packaging/     RPM spec (see RELEASING.md)
.copr/         Copr "Build from SCM" entry point (see RELEASING.md)
```
