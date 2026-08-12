set(SRCS sdl3_resource_manager.cpp sdl3_resource_manager.hpp)

add_library(sdl3_client_res STATIC ${SRCS})
target_link_libraries(sdl3_client_res PUBLIC SDL3::SDL3)
target_link_libraries(sdl3_client_res PUBLIC sdl-common-client-res)
target_include_directories(sdl3_client_res PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(sdl3_client_res PROPERTIES POSITION_INDEPENDENT_CODE ON INTERPROCEDURAL_OPTIMIZATION OFF)
