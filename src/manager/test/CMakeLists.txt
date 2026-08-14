add_executable(test-coffee-profiles test_coffee_profiles.cpp)
target_link_libraries(test-coffee-profiles PRIVATE coffee-profiles)

add_test(NAME coffee-profiles COMMAND test-coffee-profiles)
