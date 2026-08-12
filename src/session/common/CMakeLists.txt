add_subdirectory(aad)
add_subdirectory(res)

set(SRCS sdl_prefs.hpp sdl_prefs.cpp scoped_guard.hpp sdl_common_utils.hpp sdl_common_utils.cpp)

add_library(sdl-common-prefs STATIC ${SRCS})
target_link_libraries(sdl-common-prefs winpr freerdp)
