add_executable(test-coffee-quality test_coffee_quality.cpp)
target_link_libraries(test-coffee-quality PRIVATE coffee-quality)

add_test(NAME coffee-quality COMMAND test-coffee-quality)
