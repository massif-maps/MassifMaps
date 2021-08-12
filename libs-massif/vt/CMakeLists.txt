project(vt)

set(vt_SRC_DIR "${PROJECT_SOURCE_DIR}/src/vt")
set(vt_LIBS_DIR "${PROJECT_SOURCE_DIR}/../../libs-external")

file(GLOB vt_SRC_FILES "${vt_SRC_DIR}/*.cpp" "${vt_SRC_DIR}/*.h")

if(EXCLUDE_GL)
  list(FILTER vt_SRC_FILES EXCLUDE REGEX ".*(GLExtensions|GLShaderManager|GLTileRenderer).cpp$")
endif(EXCLUDE_GL)

set(vt_FLAGS "")
if(ANDROID OR APPLE)
  set(vt_FLAGS ${vt_FLAGS} "-fno-math-errno" "-fno-trapping-math")
endif()

set(vt_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src" PARENT_SCOPE)

if(SINGLE_LIBRARY)
  set(vt_SRC_FILES ${vt_SRC_FILES} PARENT_SCOPE)
  set(vt_FLAGS ${vt_FLAGS} PARENT_SCOPE)
  add_library(vt INTERFACE)
else()
  add_compile_options(${vt_FLAGS})
  include_directories(
    "${PROJECT_SOURCE_DIR}/src"
    "${vt_LIBS_DIR}/pbf"
    "${vt_LIBS_DIR}/utf8/source"
    "${vt_LIBS_DIR}/tess2/include"
    "${vt_LIBS_DIR}/freetype/freetype/include"
    "${vt_LIBS_DIR}/harfbuzz/harfbuzz/src"
    "${vt_LIBS_DIR}/bidi"
    "${vt_LIBS_DIR}/msdfgen"
    "${vt_LIBS_DIR}/cglib"
    "${vt_LIBS_DIR}/stdext"
  )
  add_library(vt OBJECT ${vt_SRC_FILES})
endif()
