cmake_minimum_required(VERSION 3.1)
project(css2xml)

# Fix behavior of CMAKE_CXX_STANDARD when targeting macOS.
if(POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif()

option(EXCLUDE_GL "Exclude GL rendering code" ON)
option(SINGLE_LIBRARY "Compile as single library" OFF)

if(WIN32 AND NOT DEFINED Imagination_SDK_ROOT)
  set(Imagination_SDK_ROOT "C:/Imagination/PowerVR_Graphics/PowerVR_SDK/SDK_2016_R2")
endif()

add_definitions("${SDK_CPP_DEFINES} -DBOOST_ALL_NO_LIB")

if(WIN32)
  add_definitions("-DNOMINMAX -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -DGL_GLEXT_PROTOTYPES")
  set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} /wd4244")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17 /wd4244 /EHs /GR /bigobj")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

set(BASE_DIR "${PROJECT_SOURCE_DIR}/../..")
set(LIBS_DIR "${PROJECT_SOURCE_DIR}/../../../libs-external")

include_directories(
  "${BASE_DIR}/cartocss/src"
  "${BASE_DIR}/mapnikvt/src"
  "${BASE_DIR}/vt/src"
  "${LIBS_DIR}/boost"
  "${LIBS_DIR}/cglib"
  "${LIBS_DIR}/stdext"
  "${LIBS_DIR}/picojson"
  "${LIBS_DIR}/pbf"
  "${LIBS_DIR}/zlib/config"
  "${LIBS_DIR}/zlib/zlib"
  "${LIBS_DIR}/bidi"
  "${LIBS_DIR}/msdfgen"
  "${LIBS_DIR}/tess2/include"
  "${LIBS_DIR}/freetype/include"
  "${LIBS_DIR}/harfbuzz/src"
  "${LIBS_DIR}/pugixml/pugixml/src"
  "${LIBS_DIR}/rapidjson/include"
  "${LIBS_DIR}/utf8/source"
)

if(WIN32)
  include_directories(
    "${Imagination_SDK_ROOT}/Builds/Include"
  )
endif()

add_subdirectory("${BASE_DIR}/vt" vt)
add_subdirectory("${BASE_DIR}/mapnikvt" mapnikvt)
add_subdirectory("${BASE_DIR}/cartocss" cartocss)
add_subdirectory("${LIBS_DIR}/bidi" bidi)
add_subdirectory("${LIBS_DIR}/freetype" freetype)
add_subdirectory("${LIBS_DIR}/harfbuzz" harfbuzz)
add_subdirectory("${LIBS_DIR}/msdfgen" msdfgen)
add_subdirectory("${LIBS_DIR}/brotli" brotli)
add_subdirectory("${LIBS_DIR}/miniz" miniz)
add_subdirectory("${LIBS_DIR}/pugixml" pugixml)
add_subdirectory("${LIBS_DIR}/tess2" tess2)
add_subdirectory("${LIBS_DIR}/zlib/zlib" zlib)

add_executable(css2xml css2xml.cpp)
target_link_libraries(css2xml cartocss mapnikvt vt pugixml tess2 brotli miniz zlib freetype harfbuzz bidi msdfgen)
if(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_libraries(css2xml "${Imagination_SDK_ROOT}/Builds/Windows/x86_32/Lib/libEGL.lib" "${Imagination_SDK_ROOT}/Builds/Windows/x86_32/Lib/libGLESv2.lib")
  else()
    target_link_libraries(css2xml "${Imagination_SDK_ROOT}/Builds/Windows/x86_64/Lib/libEGL.lib" "${Imagination_SDK_ROOT}/Builds/Windows/x86_64/Lib/libGLESv2.lib")
  endif()
endif()
