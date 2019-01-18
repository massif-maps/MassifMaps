cmake_minimum_required(VERSION 2.6)
project(mbvtbuilder)

if(WIN32)
add_definitions("-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS")
endif(WIN32)

if(APPLE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11") 
endif(APPLE)

if(CMAKE_COMPILER_IS_GNUCXX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11") 
endif(CMAKE_COMPILER_IS_GNUCXX)

set(mbvtbuilder_SRC_DIR "${PROJECT_SOURCE_DIR}/src/mbvtbuilder")
set(mbvtbuilder_LIBS_DIR "${PROJECT_SOURCE_DIR}/../libs-external")

file(GLOB mbvtbuilder_SRC_FILES "${mbvtbuilder_SRC_DIR}/*.cpp" "${mbvtbuilder_SRC_DIR}/*.h")

if(SINGLE_LIBRARY)
add_library(mbvtbuilder INTERFACE)
set(mbvtbuilder_SRC_FILES ${mbvtbuilder_SRC_FILES} PARENT_SCOPE)
else()
add_library(mbvtbuilder OBJECT ${mbvtbuilder_SRC_FILES})
endif()
