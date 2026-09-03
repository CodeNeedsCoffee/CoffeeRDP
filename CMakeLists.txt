cmake_minimum_required(VERSION 3.19)
# Bump alongside packaging/coffeerdp.spec's Version: and any git tag used to
# cut a release -- the three aren't wired together automatically, see
# PACKAGING.md's release checklist.
project(CoffeeRDP VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

enable_testing()

# Vendored FreeRDP (see vendor/freerdp, a git submodule, and
# FREERDP_VENDORING.md for what's patched and why). Building it in-tree
# via add_subdirectory() rather than depending on the system freerdp-devel
# package means the coffeerdp package is fully self-contained -- users get
# our FreeRDP fixes without needing a separate, differently-patched system
# package. FreeRDP's own targets are already named exactly what
# src/session and src/manager link against (`freerdp`, `freerdp-client`,
# `winpr` -- see libfreerdp/CMakeLists.txt, client/common/CMakeLists.txt,
# winpr/libwinpr/CMakeLists.txt), so nothing downstream needs to change to
# consume them via add_subdirectory() instead of find_package().
#
# Force a client-only build: CoffeeRDP never needs FreeRDP's server,
# shadow, proxy, or sample binaries, and building them would pull in a
# pile of extra dependencies (pam-devel, libusb, libpcsclite, ...) for
# nothing. Keep everything CoffeeRDP actually uses explicitly ON rather
# than relying on FreeRDP's own defaults, since e.g. WITH_VAAPI defaults
# OFF upstream -- this is the exact flag set validated end-to-end during
# the multimon performance/crash investigation (see
# QUALITY_PRESET_CRASH_INVESTIGATION.md).
set(WITH_SERVER OFF CACHE BOOL "" FORCE)
set(WITH_SHADOW OFF CACHE BOOL "" FORCE)
set(WITH_PROXY OFF CACHE BOOL "" FORCE)
set(WITH_SAMPLE OFF CACHE BOOL "" FORCE)
set(WITH_CLIENT ON CACHE BOOL "" FORCE)
# NOT WITH_CLIENT_SDL3: that builds FreeRDP's own bundled SDL3 sample
# client (vendor/freerdp/client/SDL/SDL3), which src/session was
# originally forked from and still shares target names with
# (sdl-common-prefs, sdl3-dialogs, sdl3_client_res, sdl-common-aad-view --
# see src/session/common and src/session/dialogs). Building both trees
# collides on those names. CoffeeRDP only needs the libraries
# (freerdp, freerdp-client, winpr), which come from WITH_CLIENT above,
# not FreeRDP's own sample client binary.
set(WITH_CLIENT_SDL3 OFF CACHE BOOL "" FORCE)
set(WITH_AAD ON CACHE BOOL "" FORCE)
set(WITH_FFMPEG ON CACHE BOOL "" FORCE)
set(WITH_VIDEO_FFMPEG ON CACHE BOOL "" FORCE)
set(WITH_VAAPI ON CACHE BOOL "" FORCE)
set(WITH_OPENH264 ON CACHE BOOL "" FORCE)
set(WITH_WAYLAND ON CACHE BOOL "" FORCE)
set(WITH_X11 ON CACHE BOOL "" FORCE)
# OSS is a legacy audio API CoffeeRDP has no use for (PulseAudio/PipeWire/
# ALSA cover real needs) -- and its channel CMakeLists.txt has an upstream
# bug that only surfaces when vendored: it configures a template via
# ${CMAKE_SOURCE_DIR}/cmake/oss-includes.h.in, which resolves to the *top*
# project's source dir (CoffeeRDP's root) rather than FreeRDP's own, since
# CMAKE_SOURCE_DIR doesn't change across add_subdirectory() boundaries.
# Disabling it entirely sidesteps the bug without patching the fork.
set(WITH_OSS OFF CACHE BOOL "" FORCE)
# USB device redirection (urbdrc, [MS-RDPEUSB]) defaults ON for a client
# build, but CoffeeRDP has never tested or documented it and it pulls in
# libusb -- disable it for the same "don't build what we don't use"
# reason as the WITH_SERVER/SHADOW/PROXY/SAMPLE block above.
set(CHANNEL_URBDRC OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/freerdp EXCLUDE_FROM_ALL)

# FreeRDP's own CMake only wires up its public headers for the
# INSTALL_INTERFACE (i.e. consumers using find_package() against a real
# `make install`) -- see e.g. vendor/freerdp/libfreerdp/CMakeLists.txt's
# target_include_directories(freerdp INTERFACE $<INSTALL_INTERFACE:...>).
# There's no equivalent BUILD_INTERFACE entry, so consuming it via
# add_subdirectory() pre-install leaves nothing propagated to callers via
# target_link_libraries(). freerdp-client links freerdp/winpr PUBLIC
# (client/common/CMakeLists.txt), so patching the include dirs onto just
# these two targets here is enough to fix it everywhere downstream,
# without touching any target_link_libraries() call in src/.
target_include_directories(freerdp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/freerdp/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/vendor/freerdp/include>
)
target_include_directories(winpr INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/freerdp/winpr/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/vendor/freerdp/winpr/include>
)

# EXCLUDE_FROM_ALL above suppresses every install() rule FreeRDP's own
# CMake would otherwise run for vendor/freerdp -- headers, pkgconfig, docs,
# its own xfreerdp/wlfreerdp sample binaries -- confirmed empty in a real
# RPM build. It also means the 3 shared libs CoffeeRDP's binaries actually
# link (freerdp, freerdp-client, winpr) are never installed at all. Left
# unfixed, a packaged coffeerdp binary falls back to whatever the dynamic
# linker's default search path finds: it silently picks up a *different*,
# unpatched system freerdp-libs package if one happens to be installed
# (identical SONAME, so nothing errors -- it's just quietly not running our
# fork, defeating the entire point of vendoring it), or fails to start at
# all if not. Confirmed both the missing install and the silent-wrong-lib
# failure mode with a real local RPM build + ldd against this dev machine's
# leftover system freerdp package before adding this block.
#
# Fix: install these three explicitly, into a private, coffeerdp-owned
# directory rather than the system libdir, so there's never a file
# conflict with a real freerdp-libs package and never an ambiguity about
# which .so a coffeerdp binary is actually running. NAMELINK_SKIP omits
# the unversioned dev symlink (libfreerdp3.so -> libfreerdp3.so.3.30.0),
# which is only needed for linking against these libs, not for running
# against them -- nothing outside this build does that.
include(GNUInstallDirs)
set(COFFEERDP_VENDOR_LIBDIR "${CMAKE_INSTALL_LIBDIR}/coffeerdp")
install(TARGETS freerdp winpr freerdp-client
    LIBRARY DESTINATION ${COFFEERDP_VENDOR_LIBDIR}
    NAMELINK_SKIP
)

add_subdirectory(src/auth-ipc)
add_subdirectory(src/session)
add_subdirectory(src/auth)
add_subdirectory(src/manager)
add_subdirectory(src/op-askpass)
