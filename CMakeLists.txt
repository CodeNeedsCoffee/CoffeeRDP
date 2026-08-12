cmake_minimum_required(VERSION 3.19)
project(CoffeeRDP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

enable_testing()

add_subdirectory(src/session)
add_subdirectory(src/auth)
