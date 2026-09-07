cmake_minimum_required(VERSION 3.1)
project(mbvtutils)

# Fix behavior of CMAKE_CXX_STANDARD when targeting macOS.
if(POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif()

find_package(Boost)

if(WIN32)
  add_definitions("-DNOMINMAX -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS")
  set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} /wd4244")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17 /wd4244 /EHs /GR /bigobj")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

set(BASE_DIR "${PROJECT_SOURCE_DIR}/../..")
if(EXISTS "${PROJECT_SOURCE_DIR}/../../../mobile-external-libs")
  set(LIBS_DIR "${PROJECT_SOURCE_DIR}/../../../mobile-external-libs")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/../../../libs-external")
  set(LIBS_DIR "${PROJECT_SOURCE_DIR}/../../../libs-external")
else()
  message(SEND_ERROR "mobile-external-libs dependency is missing")
endif()

include_directories(
  "${BASE_DIR}/mbvtbuilder/src"
  "${LIBS_DIR}/cglib"
  "${LIBS_DIR}/stdext"
  "${LIBS_DIR}/picojson"
  "${LIBS_DIR}/pbf"
  "${Boost_INCLUDE_DIRS}"
)

link_directories(
  "${Boost_LIBRARY_DIRS}"
)

add_subdirectory("${BASE_DIR}/mbvtbuilder" mbvtbuilder)

add_executable(geojson2mvt geojson2mvt.cpp)
target_link_libraries(geojson2mvt mbvtbuilder)
