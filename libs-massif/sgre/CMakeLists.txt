cmake_minimum_required(VERSION 3.1)
project(sgre)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
add_definitions("-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

set(sgre_SRC_DIR "${PROJECT_SOURCE_DIR}/src/sgre")
set(sgre_LIBS_DIR "${PROJECT_SOURCE_DIR}/../libs-external")

file(GLOB sgre_SRC_FILES "${sgre_SRC_DIR}/*.cpp" "${sgre_SRC_DIR}/*.h")

if(SINGLE_LIBRARY)
add_library(sgre INTERFACE)
set(sgre_SRC_FILES ${sgre_SRC_FILES} PARENT_SCOPE)
else()
add_library(sgre OBJECT ${sgre_SRC_FILES})
endif()
