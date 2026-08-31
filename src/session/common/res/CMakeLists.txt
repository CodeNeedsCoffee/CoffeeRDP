set(SRCS sdl_resource_manager.cpp sdl_resource_manager.hpp sdl_resource_file.cpp sdl_resource_file.hpp)

# Resources are loaded from disk at runtime (<root>/<type>/<id>) rather than
# compiled into the binary as hex arrays -- avoids depending on FreeRDP's
# private ConvertFileToHexArray cmake module, which isn't part of the
# installed FreeRDP3 package. See sdl_resource_manager.cpp::filename().
#
# Two candidate roots are baked in, checked at runtime in that order:
#  - SDL_RESOURCE_ROOT_INSTALLED: where `install()` below puts resources/,
#    under CMAKE_INSTALL_PREFIX. For a packaged build (RPM/Copr) this is a
#    stable path (e.g. /usr/share/coffeerdp/resources) that still exists
#    after the build tree is discarded.
#  - SDL_RESOURCE_ROOT_SRCTREE: the source checkout's resources/ directly,
#    for uninstalled dev builds (`ninja` without `ninja install`) run in
#    place -- CMAKE_SOURCE_DIR is only valid on the machine that built it,
#    which is fine for that case but not for a relocated/packaged binary.
set(SDL_RESOURCE_ROOT_INSTALLED "${CMAKE_INSTALL_PREFIX}/share/coffeerdp/resources")
set(SDL_RESOURCE_ROOT_SRCTREE "${CMAKE_SOURCE_DIR}/resources")
add_compile_definitions(
	SDL_RESOURCE_ROOT_INSTALLED="${SDL_RESOURCE_ROOT_INSTALLED}"
	SDL_RESOURCE_ROOT_SRCTREE="${SDL_RESOURCE_ROOT_SRCTREE}")

install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/" DESTINATION "${SDL_RESOURCE_ROOT_INSTALLED}")

add_library(sdl-common-client-res STATIC ${SRCS})
set_property(TARGET sdl-common-client-res PROPERTY POSITION_INDEPENDENT_CODE ON)

# PUBLIC so consumers resolve <res/sdl_resource_manager.hpp> (dialogs/res)
# and <aad/sdl_webview.hpp> (session sources) against client/SDL/common as
# the include root, matching upstream FreeRDP's layout.
target_include_directories(sdl-common-client-res PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
