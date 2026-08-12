set(SRCS sdl_resource_manager.cpp sdl_resource_manager.hpp sdl_resource_file.cpp sdl_resource_file.hpp)

# Resources are loaded from disk at runtime (SDL_RESOURCE_ROOT/<type>/<id>)
# rather than compiled into the binary as hex arrays -- avoids depending on
# FreeRDP's private ConvertFileToHexArray cmake module, which isn't part of
# the installed FreeRDP3 package. See sdl_resource_manager.cpp::filename().
set(SDL_RESOURCE_ROOT "${CMAKE_SOURCE_DIR}/resources")
add_compile_definitions(SDL_RESOURCE_ROOT="${SDL_RESOURCE_ROOT}")

add_library(sdl-common-client-res STATIC ${SRCS})
set_property(TARGET sdl-common-client-res PROPERTY POSITION_INDEPENDENT_CODE ON)

# PUBLIC so consumers resolve <res/sdl_resource_manager.hpp> (dialogs/res)
# and <aad/sdl_webview.hpp> (session sources) against client/SDL/common as
# the include root, matching upstream FreeRDP's layout.
target_include_directories(sdl-common-client-res PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
