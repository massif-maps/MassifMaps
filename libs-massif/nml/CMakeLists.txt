cmake_minimum_required(VERSION 3.1)
project(nml)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
add_definitions("-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

set(nml_SRC_DIR "${PROJECT_SOURCE_DIR}/src/nml")
set(nml_LIBS_DIR "${PROJECT_SOURCE_DIR}/../libs-external")

file(GLOB nml_SRC_FILES "${nml_SRC_DIR}/*.cpp" "${nml_SRC_DIR}/*.h")

if(SINGLE_LIBRARY)
add_library(nml INTERFACE)
set(nml_SRC_FILES ${nml_SRC_FILES} PARENT_SCOPE)
else()
add_library(nml OBJECT ${nml_SRC_FILES})
endif()
