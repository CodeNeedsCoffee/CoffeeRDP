# FREERDP_ASKPASS helper backed by the 1Password CLI (`op`), spawned by
# coffee_rdp_manager.cpp's connectToProfile() when a profile has an
# onePasswordRef set. See coffee_rdp_op_askpass.cpp for the actual contract.
# No GTK/FreeRDP dependency at all -- it only needs libc.
set(MODULE_NAME "coffee-rdp-op-askpass")

add_executable(${MODULE_NAME} coffee_rdp_op_askpass.cpp)

install(TARGETS ${MODULE_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
