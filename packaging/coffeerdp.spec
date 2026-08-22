Name:           coffeerdp
Version:        0.1.0
Release:        4%{?dist}
Summary:        RDP client with durable Entra ID login, built on FreeRDP's SDL3 client

License:        Apache-2.0
URL:            https://github.com/CodeNeedsCoffee/CoffeeRDP
# .copr/Makefile builds this from `git archive` at the commit COPR checks
# out -- there is no tagged upstream release tarball yet, see PACKAGING.md.
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig
# freerdp3.pc / freerdp-client3.pc -- the Fedora package predates the
# FreeRDP-3-specific naming the upstream .pc files themselves use, hence the
# mismatch between this BuildRequires and the "3" everywhere else here.
BuildRequires:  freerdp-devel
BuildRequires:  libwinpr-devel
# Not a compile-time dependency at all, but a configure-time one: WinPR's
# installed CMake config (/usr/lib64/cmake/WinPR3/WinPRTargets.cmake, pulled
# in by FreeRDPConfig.cmake -> find_dependency(WinPR), which our
# src/session/CMakeLists.txt's find_package(FreeRDP 3) reaches) declares an
# IMPORTED target for the winpr-makecert *binary* and hard-fails configure if
# the file is missing. That binary ships in the main `freerdp` package, while
# freerdp-devel only requires `freerdp-libs`, so a clean chroot has the CMake
# config but not the executable it points at. Invisible on a dev box (where
# `freerdp` is installed anyway) and it's exactly what broke Copr build
# 10889775 on all three chroots. Requiring the path rather than the package
# keeps this correct if Fedora ever moves the WinPR tools elsewhere.
BuildRequires:  /usr/bin/winpr-makecert
BuildRequires:  SDL3-devel
BuildRequires:  SDL3_ttf-devel
BuildRequires:  gtk4-devel
BuildRequires:  libadwaita-devel
BuildRequires:  webkitgtk6.0-devel
# gdbus-codegen (the resident auth helper's GDBus interface, PLAN.md Phase
# 7.5) and gio-2.0/gio-unix-2.0 already come in transitively via gtk4-devel,
# but this project talks to them directly, not just through GTK, so it's
# named explicitly rather than left implicit.
BuildRequires:  glib2-devel
# XDG Background portal client (run in the background past the manager
# window closing, so the resident auth helper survives it too), Phase 7.5.
BuildRequires:  libportal-gtk4-devel
# %%check: validates the .desktop file and AppStream metadata at build time
# so a broken one fails the package build, not a GNOME Software listing
# after the fact.
BuildRequires:  desktop-file-utils
BuildRequires:  appstream

# The session client renders through the GPU (or the compositor's software
# fallback) via SDL3, same runtime story as any other SDL/GTK application --
# no separate Requires needed beyond what the dependent libraries above
# already pull in transitively.

%description
CoffeeRDP is a minimal RDP client for Linux, built on FreeRDP's SDL3
client with a GTK4 connection manager on top of it.

Features:
 * Wayland-first, with an automatic X11 fallback when Wayland can't deliver
 * Persistent Entra ID (Azure AD) login instead of a full MFA challenge
   every connection, reusing one signed-in session for as long as coffeerdp
   keeps running (optionally in the background past closing its window)
 * An idle keep-alive so sessions don't disconnect while you're away
 * An in-session control bar: disconnect, minimize, fullscreen/restore,
   keyboard capture, Ctrl+Alt+Del, Send Super, connection quality, and a
   local-shortcuts toggle
 * Selectable connection quality presets
 * A connection manager with saved profiles and in-place .rdp file editing

This package installs three binaries: coffeerdp (the GTK4 connection
manager -- the one with a desktop launcher), coffee-rdp-session (the SDL3
session client it launches per connection), and coffee-rdp-auth (a
WebKitGTK-based Entra ID sign-in helper). coffeerdp starts coffee-rdp-auth
as a resident helper on first connect and keeps it running, over its own
private connection, for as long as coffeerdp is open; closing the window
offers to keep it running in the background (via the XDG Background
portal) so that session survives past the window too.

%prep
%autosetup -p1

%build
%cmake -GNinja
%cmake_build

%install
%cmake_install

%check
%ctest
desktop-file-validate %{buildroot}%{_datadir}/applications/com.codeneedscoffee.coffeerdp.manager.desktop
desktop-file-validate %{buildroot}%{_datadir}/applications/com.codeneedscoffee.coffeerdp.session.desktop
appstreamcli validate --no-net --pedantic \
    %{buildroot}%{_datadir}/metainfo/com.codeneedscoffee.coffeerdp.manager.metainfo.xml

%files
%license LICENSE
%doc README.md
%{_bindir}/coffeerdp
%{_bindir}/coffee-rdp-session
%{_bindir}/coffee-rdp-auth
%{_datadir}/applications/com.codeneedscoffee.coffeerdp.manager.desktop
%{_datadir}/applications/com.codeneedscoffee.coffeerdp.session.desktop
%{_datadir}/metainfo/com.codeneedscoffee.coffeerdp.manager.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/com.codeneedscoffee.coffeerdp.manager.png

%changelog
* Fri Aug 21 2026 CodeNeedsCoffee <codeneedscoffee@gmail.com> - 0.1.0-4
- Give coffee-rdp-session a NoDisplay desktop entry matching its Wayland
  app_id/X11 WM_CLASS (com.codeneedscoffee.coffeerdp.session), reusing the
  manager's icon. Without it, GNOME had nothing to resolve that id
  against and showed the raw string with a generic icon in the dock,
  alt-tab, and window tooltips instead of the CoffeeRDP name and icon.

* Fri Aug 21 2026 CodeNeedsCoffee <codeneedscoffee@gmail.com> - 0.1.0-3
- Fix the build failing in a clean chroot (Copr build 10889775, all three
  targets): WinPR's CMake config declares an imported target for the
  winpr-makecert binary, which lives in the main `freerdp` package rather
  than freerdp-devel, so configure hard-failed where it had always
  succeeded on a dev box that happened to have `freerdp` installed. Added
  the missing BuildRequires.

* Fri Aug 21 2026 CodeNeedsCoffee <codeneedscoffee@gmail.com> - 0.1.0-2
- Resident coffee-rdp-auth helper (Phase 7.5): coffeerdp now starts and
  owns a long-lived auth helper on first connect instead of a fresh one
  per connection, so a reconnect while it's open reuses the same signed-in
  WebView. Adds an XDG Background portal request on window close so that
  helper can keep running past the window closing too. New build/runtime
  dependency: libportal-gtk4(-devel).

* Thu Aug 20 2026 CodeNeedsCoffee <codeneedscoffee@gmail.com> - 0.1.0-1
- First packaged release: session client, quality presets, idle keep-alive,
  the in-session floatbar, persistent Entra ID login, and the GTK4
  connection manager.
