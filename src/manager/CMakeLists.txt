set(MODULE_NAME "coffeerdp")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)
# Owns the resident coffee-rdp-auth --serve helper (GSubprocess, GDBus) and
# the XDG Background portal request on window close (PLAN.md Phase 7.5).
pkg_check_modules(LIBPORTAL REQUIRED IMPORTED_TARGET libportal-gtk4)
# Headers only -- coffee-rdp-document uses FreeRDP's CONNECTION_TYPE_*
# constants so they can't drift from the library we build against.
# The FreeRDP target itself comes from the vendored add_subdirectory() in
# the top-level CMakeLists.txt, not find_package() -- see the comment there.

# Profile model + INI store. No GTK dependency at all -- same reasoning as
# coffee-quality/coffee-idle/coffee-floatbar in src/session: the load/save
# and validation logic is what's worth testing, and testing it shouldn't
# need a display server.
add_library(coffee-profiles STATIC coffee_profiles.cpp coffee_profiles.hpp)
target_include_directories(coffee-profiles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# In-place .rdp editing (read, change modeled keys, write back preserving
# everything else). GTK-free for the same reason as coffee-profiles.
add_library(coffee-rdp-document STATIC coffee_rdp_document.cpp coffee_rdp_document.hpp)
target_link_libraries(coffee-rdp-document PUBLIC coffee-profiles freerdp)

enable_testing()
add_subdirectory(test)

add_executable(${MODULE_NAME} coffee_rdp_manager.cpp)
target_link_libraries(${MODULE_NAME} PRIVATE coffee-profiles coffee-rdp-document PkgConfig::GTK4
                                            PkgConfig::ADWAITA PkgConfig::GIO PkgConfig::LIBPORTAL
                                            coffee-auth-ipc)

install(TARGETS ${MODULE_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

# Vendored freerdp/winpr install to a private, coffeerdp-owned lib
# directory, not the system libdir -- see the COFFEERDP_VENDOR_LIBDIR
# comment in the top-level CMakeLists.txt. This binary installs to bin/,
# so it's one level up from there.
set_target_properties(${MODULE_NAME} PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${COFFEERDP_VENDOR_LIBDIR}"
    BUILD_WITH_INSTALL_RPATH FALSE
)

# Desktop entry + icon (PLAN.md Phase 7.4). Icon= in the .desktop file names
# the icon by its hicolor theme id ("com.codeneedscoffee.coffeerdp.manager"),
# not a path -- every size below installs under that same name so the
# desktop shell's own theme lookup finds it, the standard XDG mechanism
# rather than anything CoffeeRDP-specific.
install(FILES data/com.codeneedscoffee.coffeerdp.manager.desktop
       DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)

# AppStream metadata -- what GNOME Software (and any other AppStream-reading
# software center) actually reads to list the app, show its description, and
# render it as more than a bare launcher entry. id must match the desktop
# file's own filename (including ".desktop"), which is what ties the two
# together -- see the metainfo file's own <launchable> for the same pairing
# from the other direction.
install(FILES data/com.codeneedscoffee.coffeerdp.manager.metainfo.xml
       DESTINATION ${CMAKE_INSTALL_PREFIX}/share/metainfo)

set(COFFEERDP_ICON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../resources/icon)
foreach(_size 16 24 32 48 64 128 256)
   install(FILES ${COFFEERDP_ICON_SOURCE_DIR}/hicolor/${_size}x${_size}/apps/com.codeneedscoffee.coffeerdp.manager.png
          DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/${_size}x${_size}/apps)
endforeach()
