Name:           coffeerdp
Version:        0.1.0
Release:        1%{?dist}
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
BuildRequires:  SDL3-devel
BuildRequires:  SDL3_ttf-devel
BuildRequires:  gtk4-devel
BuildRequires:  libadwaita-devel
BuildRequires:  webkitgtk6.0-devel
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
   every connection
 * An idle keep-alive so sessions don't disconnect while you're away
 * An in-session control bar: disconnect, minimize, fullscreen/restore,
   keyboard capture, Ctrl+Alt+Del, Send Super, connection quality, and a
   local-shortcuts toggle
 * Selectable connection quality presets
 * A connection manager with saved profiles and in-place .rdp file editing

This package installs three binaries: coffeerdp (the GTK4 connection
manager -- the one with a desktop launcher), coffee-rdp-session (the SDL3
session client it launches per connection), and coffee-rdp-auth (a
WebKitGTK-based Entra ID sign-in helper run as its own subprocess so a
renderer crash can't take the session down with it).

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
appstreamcli validate --no-net --pedantic \
    %{buildroot}%{_datadir}/metainfo/com.codeneedscoffee.coffeerdp.manager.metainfo.xml

%files
%license LICENSE
%doc README.md
%{_bindir}/coffeerdp
%{_bindir}/coffee-rdp-session
%{_bindir}/coffee-rdp-auth
%{_datadir}/applications/com.codeneedscoffee.coffeerdp.manager.desktop
%{_datadir}/metainfo/com.codeneedscoffee.coffeerdp.manager.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/com.codeneedscoffee.coffeerdp.manager.png

%changelog
* Thu Aug 20 2026 CodeNeedsCoffee <codeneedscoffee@gmail.com> - 0.1.0-1
- First packaged release: session client, quality presets, idle keep-alive,
  the in-session floatbar, persistent Entra ID login, and the GTK4
  connection manager.
