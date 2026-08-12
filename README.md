# CoffeeRDP

A minimal RDP client for Linux, built on [FreeRDP](https://www.freerdp.com/)'s
SDL3 client.

Goals:

- Wayland-first, with an automatic X11 fallback when Wayland can't deliver
  (multi-monitor placement, global key capture)
- Persistent Entra ID (AAD) login instead of a full MFA challenge on every
  connection
- An idle keep-alive so sessions don't disconnect while you're away
- An in-session control bar (disconnect, fullscreen, keyboard grab, send
  Ctrl+Alt+Del, connection quality)
- Selectable connection quality presets (speed vs. image fidelity)

## Status

Early development. The session client (`coffee-rdp-session`) currently builds
and runs at parity with upstream `sdl-freerdp` against the installed system
FreeRDP 3 libraries. No CoffeeRDP-specific features are implemented yet.

## Building

Requires FreeRDP 3's development packages (`freerdp3`, `freerdp-client3`,
`winpr3`) installed and discoverable via `find_package`, plus SDL3 and
SDL3_ttf.

```sh
cmake -S . -B build -GNinja
ninja -C build
```

The session client binary is built at `build/src/session/coffee-rdp-session`.

## Layout

```
src/session/   The SDL3-based RDP session client (forked from FreeRDP's
               client/SDL/SDL3 and client/SDL/common)
resources/     Bundled assets (fonts) loaded by the session client at runtime
```
