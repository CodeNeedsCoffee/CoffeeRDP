cmake_minimum_required(VERSION 3.19)
# Bump alongside packaging/coffeerdp.spec's Version: and any git tag used to
# cut a release -- the three aren't wired together automatically, see
# PACKAGING.md's release checklist.
project(CoffeeRDP VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

enable_testing()

add_subdirectory(src/auth-ipc)
add_subdirectory(src/session)
add_subdirectory(src/auth)
add_subdirectory(src/manager)
