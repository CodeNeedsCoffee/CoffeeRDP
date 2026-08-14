set(SRCS
    sdl_button.hpp
    sdl_button.cpp
    sdl_buttons.hpp
    sdl_buttons.cpp
    sdl_dialogs.cpp
    sdl_dialogs.hpp
    sdl_widget.hpp
    sdl_widget.cpp
    sdl_widget_list.hpp
    sdl_widget_list.cpp
    sdl_input_widget.hpp
    sdl_input_widget.cpp
    sdl_input_widget_pair.hpp
    sdl_input_widget_pair.cpp
    sdl_input_widget_pair_list.hpp
    sdl_input_widget_pair_list.cpp
    sdl_select.hpp
    sdl_select.cpp
    sdl_select_list.hpp
    sdl_select_list.cpp
    sdl_selectable_widget.cpp
    sdl_selectable_widget.hpp
    sdl_connection_dialog.cpp
    sdl_connection_dialog.hpp
    sdl_connection_dialog_wrapper.cpp
    sdl_connection_dialog_wrapper.hpp
    sdl_connection_dialog_hider.hpp
    sdl_connection_dialog_hider.cpp
    sdl_blend_mode_guard.cpp
    sdl_blend_mode_guard.hpp
    sdl_floatbar.cpp
    sdl_floatbar.hpp
)

add_subdirectory(res)

add_library(sdl3-dialogs STATIC ${SRCS})
target_link_libraries(sdl3-dialogs PUBLIC sdl3_client_res winpr freerdp SDL3::SDL3 SDL3_ttf::SDL3_ttf coffee-floatbar)
