cmake_minimum_required(VERSION 3.16)
project(massif_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDK_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

enable_testing()
add_subdirectory(api)
add_subdirectory(style)
add_subdirectory(vt)
