project(mbvtbuilder)

set(mbvtbuilder_SRC_DIR "${PROJECT_SOURCE_DIR}/src/mbvtbuilder")
set(mbvtbuilder_LIBS_DIR "${PROJECT_SOURCE_DIR}/../../libs-external")

file(GLOB mbvtbuilder_SRC_FILES "${mbvtbuilder_SRC_DIR}/*.cpp" "${mbvtbuilder_SRC_DIR}/*.h")

set(mbvtbuilder_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src" PARENT_SCOPE)

if(SINGLE_LIBRARY)
  set(mbvtbuilder_SRC_FILES ${mbvtbuilder_SRC_FILES} PARENT_SCOPE)
  add_library(mbvtbuilder INTERFACE)
else()
  include_directories(
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/../mapnikvt/src"
    "${mbvtbuilder_LIBS_DIR}/picojson"
    "${mbvtbuilder_LIBS_DIR}/pbf"
    "${mbvtbuilder_LIBS_DIR}/cglib"
    "${mbvtbuilder_LIBS_DIR}/stdext"
  )
  add_library(mbvtbuilder OBJECT ${mbvtbuilder_SRC_FILES})
endif()
