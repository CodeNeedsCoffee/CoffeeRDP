# AAD login popup. Phase 4 (PLAN.md sections 2.3/4/3a): webview_impl.cpp
# spawns the separate coffee-rdp-auth process (GTK4 + webkitgtk6.0, its own
# target under src/auth/) rather than embedding a browser in this process --
# GTK3 and GTK4 can't coexist in one process, and it isolates a WebKit crash
# from taking the RDP session down with it. No GTK/WebKit/FetchContent
# dependency here at all anymore; sdl_webview.cpp's URL building
# (freerdp_client_get_aad_url) was already correct and needed no changes.
set(SRCS sdl_webview.hpp webview_impl.hpp sdl_webview.cpp webview_impl.cpp)

add_library(sdl-common-aad-view STATIC ${SRCS})
target_include_directories(sdl-common-aad-view PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(sdl-common-aad-view PRIVATE winpr freerdp)

# Lets webview_impl.cpp find its sibling coffee-rdp-auth binary in the build
# tree before anything is installed (see findAuthHelper() there).
target_compile_definitions(sdl-common-aad-view PRIVATE
    COFFEE_RDP_AUTH_BUILD_PATH="$<TARGET_FILE:coffee-rdp-auth>")
add_dependencies(sdl-common-aad-view coffee-rdp-auth)
