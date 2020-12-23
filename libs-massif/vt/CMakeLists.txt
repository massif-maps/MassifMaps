cmake_minimum_required(VERSION 3.1)
project(vt)

if(WIN32)
add_definitions("-D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS")
endif(WIN32)

if(APPLE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11") 
endif(APPLE)

if(CMAKE_COMPILER_IS_GNUCXX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11") 
endif(CMAKE_COMPILER_IS_GNUCXX)

set(vt_SRC_DIR "${PROJECT_SOURCE_DIR}/src/vt")
set(vt_LIBS_DIR "${PROJECT_SOURCE_DIR}/../libs-external")

file(GLOB vt_SRC_FILES "${vt_SRC_DIR}/*.cpp" "${vt_SRC_DIR}/*.h")

if(EXCLUDE_GL)
list(FILTER vt_SRC_FILES EXCLUDE REGEX ".*(GLExtensions|GLShaderManager|GLTileRenderer).cpp$")
endif(EXCLUDE_GL)

if(SINGLE_LIBRARY)
add_library(vt INTERFACE)
set(vt_SRC_FILES ${vt_SRC_FILES} PARENT_SCOPE)
else()
add_library(vt OBJECT ${vt_SRC_FILES})
endif()
