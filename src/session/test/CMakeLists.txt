add_executable(test-coffee-quality test_coffee_quality.cpp)
target_link_libraries(test-coffee-quality PRIVATE coffee-quality)

add_test(NAME coffee-quality COMMAND test-coffee-quality)

add_executable(test-coffee-idle test_coffee_idle.cpp)
target_link_libraries(test-coffee-idle PRIVATE coffee-idle)

add_test(NAME coffee-idle COMMAND test-coffee-idle)

add_executable(test-coffee-hwdecode test_coffee_hwdecode.cpp)
target_link_libraries(test-coffee-hwdecode PRIVATE coffee-hwdecode)

add_test(NAME coffee-hwdecode COMMAND test-coffee-hwdecode)

add_executable(test-coffee-floatbar test_coffee_floatbar.cpp)
target_link_libraries(test-coffee-floatbar PRIVATE coffee-floatbar)

add_test(NAME coffee-floatbar COMMAND test-coffee-floatbar)
