cmake_minimum_required(VERSION 3.16)
project(massif_tests C CXX) # C for tess2 and bidi, which vt's tile builder links against

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDK_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

enable_testing()
add_subdirectory(api)
add_subdirectory(style)
add_subdirectory(vt)
