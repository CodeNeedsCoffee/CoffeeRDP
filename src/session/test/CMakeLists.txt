add_executable(test-coffee-quality test_coffee_quality.cpp)
target_link_libraries(test-coffee-quality PRIVATE coffee-quality)

add_test(NAME coffee-quality COMMAND test-coffee-quality)

add_executable(test-coffee-idle test_coffee_idle.cpp)
target_link_libraries(test-coffee-idle PRIVATE coffee-idle)

add_test(NAME coffee-idle COMMAND test-coffee-idle)
