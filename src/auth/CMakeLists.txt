set(MODULE_NAME "coffee-rdp-auth")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(WEBKITGTK6 REQUIRED IMPORTED_TARGET webkitgtk-6.0)

add_executable(${MODULE_NAME} coffee_rdp_auth.cpp)
target_link_libraries(${MODULE_NAME} PRIVATE PkgConfig::GTK4 PkgConfig::WEBKITGTK6 coffee-auth-ipc)

install(TARGETS ${MODULE_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
