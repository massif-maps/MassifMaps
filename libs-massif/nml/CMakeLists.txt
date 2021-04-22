project(nml)

set(nml_SRC_DIR "${PROJECT_SOURCE_DIR}/src/nml")
set(nml_LIBS_DIR "${PROJECT_SOURCE_DIR}/../../libs-external")

file(GLOB nml_SRC_FILES "${nml_SRC_DIR}/*.cpp" "${nml_SRC_DIR}/*.h")

set(nml_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src" PARENT_SCOPE)

if(SINGLE_LIBRARY)
  set(nml_SRC_FILES ${nml_SRC_FILES} PARENT_SCOPE)
  add_library(nml INTERFACE)
else()
  include_directories(
    "${PROJECT_SOURCE_DIR}/src"
    "${nml_LIBS_DIR}/rg_etc1"
    "${nml_LIBS_DIR}/pvrt"
    "${nml_LIBS_DIR}/pbf"
    "${nml_LIBS_DIR}/cglib"
    "${nml_LIBS_DIR}/stdext"
  )
  add_library(nml OBJECT ${nml_SRC_FILES})
endif()
