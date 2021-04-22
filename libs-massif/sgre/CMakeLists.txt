project(sgre)

set(sgre_SRC_DIR "${PROJECT_SOURCE_DIR}/src/sgre")
set(sgre_LIBS_DIR "${PROJECT_SOURCE_DIR}/../../libs-external")

file(GLOB sgre_SRC_FILES "${sgre_SRC_DIR}/*.cpp" "${sgre_SRC_DIR}/*.h")

set(sgre_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src" PARENT_SCOPE)

if(SINGLE_LIBRARY)
  set(sgre_SRC_FILES ${sgre_SRC_FILES} PARENT_SCOPE)
  add_library(sgre INTERFACE)
else()
  include_directories(
    "${PROJECT_SOURCE_DIR}/src"
    "${sgre_LIBS_DIR}/cglib"
    "${sgre_LIBS_DIR}/stdext"
    "${sgre_LIBS_DIR}/picojson"
    "${sgre_LIBS_DIR}/tess2/include"
  )
  add_library(sgre OBJECT ${sgre_SRC_FILES})
endif()
