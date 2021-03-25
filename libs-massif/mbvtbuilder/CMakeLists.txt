cmake_minimum_required(VERSION 3.1)
project(mbvtbuilder)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
add_definitions("-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

set(mbvtbuilder_SRC_DIR "${PROJECT_SOURCE_DIR}/src/mbvtbuilder")
set(mbvtbuilder_LIBS_DIR "${PROJECT_SOURCE_DIR}/../libs-external")

file(GLOB mbvtbuilder_SRC_FILES "${mbvtbuilder_SRC_DIR}/*.cpp" "${mbvtbuilder_SRC_DIR}/*.h")

if(SINGLE_LIBRARY)
add_library(mbvtbuilder INTERFACE)
set(mbvtbuilder_SRC_FILES ${mbvtbuilder_SRC_FILES} PARENT_SCOPE)
else()
add_library(mbvtbuilder OBJECT ${mbvtbuilder_SRC_FILES})
endif()
