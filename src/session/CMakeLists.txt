set(MODULE_NAME "coffee-rdp-session")

find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(Threads REQUIRED)
find_package(FreeRDP 3 REQUIRED)
find_package(FreeRDP-Client 3 REQUIRED)
find_package(WinPR 3 REQUIRED)

# common must come before dialogs: dialogs/res links sdl-common-client-res,
# defined in common/res. Matches upstream client/SDL/CMakeLists.txt ordering.
add_subdirectory(common)
add_subdirectory(dialogs)

set(SRCS
    sdl_types.hpp
    sdl_utils.cpp
    sdl_utils.hpp
    sdl_input.cpp
    sdl_input.hpp
    sdl_touch.cpp
    sdl_touch.hpp
    sdl_pointer.cpp
    sdl_pointer.hpp
    sdl_disp.cpp
    sdl_disp.hpp
    sdl_monitor.cpp
    sdl_monitor.hpp
    sdl_freerdp.hpp
    sdl_freerdp.cpp
    sdl_channels.hpp
    sdl_channels.cpp
    sdl_window.hpp
    sdl_window.cpp
    sdl_clip.hpp
    sdl_clip.cpp
    sdl_context.hpp
    sdl_context.cpp
)

set(WITH_WEBVIEW ON)
set(SDL_CLIENT_BINARY_NAME "${MODULE_NAME}")
set(FREERDP_VERSION_FULL "0.1.0")
set(GIT_REVISION "coffeerdp")
set(VENDOR "CoffeeRDP")
set(SDL_CLIENT_UUID "com.coffeerdp.session")
set(PROJECT_URL "")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sdl_config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/sdl_config.hpp @ONLY)

add_executable(${MODULE_NAME} ${SRCS})
target_include_directories(${MODULE_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(${MODULE_NAME} PRIVATE
    winpr
    freerdp
    freerdp-client
    Threads::Threads
    sdl3_client_res
    sdl3-dialogs
    sdl-common-aad-view
    sdl-common-prefs
    SDL3::SDL3
)

install(TARGETS ${MODULE_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
