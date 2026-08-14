add_executable(test-coffee-profiles test_coffee_profiles.cpp)
target_link_libraries(test-coffee-profiles PRIVATE coffee-profiles)

add_test(NAME coffee-profiles COMMAND test-coffee-profiles)

add_executable(test-coffee-rdp-document test_coffee_rdp_document.cpp)
target_link_libraries(test-coffee-rdp-document PRIVATE coffee-rdp-document)

add_test(NAME coffee-rdp-document COMMAND test-coffee-rdp-document)
