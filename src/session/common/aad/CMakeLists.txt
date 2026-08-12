# AAD login webview, same akallabeth/webview fork FreeRDP vendors upstream.
# Phase 4 replaces this with an out-of-process coffee-rdp-auth helper (see
# PLAN.md sections 2.3/4/3a) -- kept as-is here so Phase 1 has strict parity
# with today's sdl-freerdp, including AAD login.
set(SRCS sdl_webview.hpp webview_impl.hpp sdl_webview.cpp)
set(LIBS winpr freerdp)

include(FetchContent)
set(WEBVIEW_BUILD_DOCS OFF CACHE INTERNAL "fetchcontent default")
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE INTERNAL "fetchcontent default")
set(WEBVIEW_BUILD_STATIC_LIBRARY ON CACHE INTERNAL "fetchcontent default")
set(WEBVIEW_BUILD_TESTS OFF CACHE INTERNAL "fetchcontent default")
set(WEBVIEW_BUILD_EXAMPLES OFF CACHE INTERNAL "fetchcontent default")
FetchContent_Declare(webview GIT_REPOSITORY https://github.com/akallabeth/webview GIT_TAG navigation-listener SYSTEM)
FetchContent_MakeAvailable(webview)

list(APPEND SRCS webview_impl.cpp)
list(APPEND LIBS webview::core)

add_library(sdl-common-aad-view STATIC ${SRCS})
target_include_directories(sdl-common-aad-view PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(sdl-common-aad-view PRIVATE ${LIBS})
