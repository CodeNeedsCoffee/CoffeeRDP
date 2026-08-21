# Shared GDBus interface between coffee-rdp-session (client) and the
# resident coffee-rdp-auth --serve helper (server), spoken peer-to-peer over
# a unix socket rather than the session bus (see auth_ipc_socket_path() in
# coffee_auth_ipc.hpp and PLAN.md Phase 7.5). One codegen run, linked by
# both src/auth (skeleton side) and src/session/common/aad (proxy side), so
# the two never drift apart.
set(MODULE_NAME "coffee-auth-ipc")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0)

find_program(GDBUS_CODEGEN gdbus-codegen REQUIRED)

set(IPC_XML ${CMAKE_CURRENT_SOURCE_DIR}/com.codeneedscoffee.coffeerdp.Auth1.xml)
set(IPC_GENERATED_BASE ${CMAKE_CURRENT_BINARY_DIR}/coffee-rdp-auth-generated)

add_custom_command(
   OUTPUT ${IPC_GENERATED_BASE}.c ${IPC_GENERATED_BASE}.h
   COMMAND ${GDBUS_CODEGEN}
           --interface-prefix com.codeneedscoffee.coffeerdp.
           --c-namespace CoffeeRdp
           --generate-c-code ${IPC_GENERATED_BASE}
           ${IPC_XML}
   DEPENDS ${IPC_XML}
   COMMENT "Generating GDBus skeleton/proxy for com.codeneedscoffee.coffeerdp.Auth1")

# Plain C (gdbus-codegen's own output, not GLib's C++ bindings), built as
# part of this otherwise-C++ project the same way WebKit/GTK's own C headers
# already are throughout src/auth and src/session.
set_source_files_properties(${IPC_GENERATED_BASE}.c PROPERTIES LANGUAGE C)

add_library(${MODULE_NAME} STATIC
   ${IPC_GENERATED_BASE}.c
   coffee_auth_ipc.cpp
   coffee_auth_ipc.hpp)
target_include_directories(${MODULE_NAME} PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}
   ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${MODULE_NAME} PUBLIC PkgConfig::GIO)
