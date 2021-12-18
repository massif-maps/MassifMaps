cmake_minimum_required(VERSION 3.1)
project(cssutils)

# Fix behavior of CMAKE_CXX_STANDARD when targeting macOS.
if(POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif()

option(EXCLUDE_GL "Exclude GL rendering code" ON)
option(SINGLE_LIBRARY "Compile as single library" OFF)

find_package(Boost)

if(WIN32)
  add_definitions("-DNOMINMAX -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -DGL_GLEXT_PROTOTYPES")
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
  "${BASE_DIR}/cartocss/src"
  "${BASE_DIR}/mapnikvt/src"
  "${BASE_DIR}/vt/src"
  "${LIBS_DIR}/cglib"
  "${LIBS_DIR}/stdext"
  "${LIBS_DIR}/picojson"
  "${LIBS_DIR}/pbf"
  "${LIBS_DIR}/zlib/config"
  "${LIBS_DIR}/zlib/zlib"
  "${LIBS_DIR}/bidi"
  "${LIBS_DIR}/tess2/include"
  "${LIBS_DIR}/freetype/freetype/include"
  "${LIBS_DIR}/harfbuzz/harfbuzz/src"
  "${LIBS_DIR}/pugixml/pugixml/src"
  "${LIBS_DIR}/rapidjson/include"
  "${LIBS_DIR}/utf8/source"
  "${Boost_INCLUDE_DIRS}"
)

link_directories(
  "${Boost_LIBRARY_DIRS}"
)

add_subdirectory("${BASE_DIR}/vt" vt)
add_subdirectory("${BASE_DIR}/mapnikvt" mapnikvt)
add_subdirectory("${BASE_DIR}/cartocss" cartocss)
add_subdirectory("${LIBS_DIR}/bidi" bidi)
add_subdirectory("${LIBS_DIR}/freetype" freetype)
add_subdirectory("${LIBS_DIR}/harfbuzz" harfbuzz)
add_subdirectory("${LIBS_DIR}/brotli" brotli)
add_subdirectory("${LIBS_DIR}/miniz" miniz)
add_subdirectory("${LIBS_DIR}/pugixml" pugixml)
add_subdirectory("${LIBS_DIR}/tess2" tess2)
add_subdirectory("${LIBS_DIR}/zlib" zlib)

add_executable(css2xml css2xml.cpp)
add_executable(mvt2xml mvt2xml.cpp)
target_link_libraries(css2xml cartocss mapnikvt vt pugixml tess2 brotli miniz zlib freetype harfbuzz bidi)
target_link_libraries(mvt2xml cartocss mapnikvt vt pugixml tess2 brotli miniz zlib freetype harfbuzz bidi)
