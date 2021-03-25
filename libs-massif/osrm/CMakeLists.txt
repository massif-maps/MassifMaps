cmake_minimum_required(VERSION 3.1)
project(osrm)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
add_definitions("-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

set(osrm_SRC_DIR "${PROJECT_SOURCE_DIR}/src/osrm")
set(osrm_LIBS_DIR "${PROJECT_SOURCE_DIR}/../libs-external")

file(GLOB osrm_SRC_FILES "${osrm_SRC_DIR}/*.cpp" "${osrm_SRC_DIR}/*.h")

if(SINGLE_LIBRARY)
add_library(osrm INTERFACE)
set(osrm_SRC_FILES ${osrm_SRC_FILES} PARENT_SCOPE)
else()
add_library(osrm OBJECT ${osrm_SRC_FILES})
endif()
