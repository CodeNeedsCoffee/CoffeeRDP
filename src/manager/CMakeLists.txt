set(MODULE_NAME "coffeerdp")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)

# Profile model + INI store. No GTK dependency at all -- same reasoning as
# coffee-quality/coffee-idle/coffee-floatbar in src/session: the load/save
# and validation logic is what's worth testing, and testing it shouldn't
# need a display server.
add_library(coffee-profiles STATIC coffee_profiles.cpp coffee_profiles.hpp)
target_include_directories(coffee-profiles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
add_subdirectory(test)

add_executable(${MODULE_NAME} coffee_rdp_manager.cpp)
target_link_libraries(${MODULE_NAME} PRIVATE coffee-profiles PkgConfig::GTK4 PkgConfig::ADWAITA)

install(TARGETS ${MODULE_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
