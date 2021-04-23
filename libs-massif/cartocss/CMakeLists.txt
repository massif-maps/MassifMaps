project(cartocss)

set(cartocss_SRC_DIR "${PROJECT_SOURCE_DIR}/src/cartocss")
set(cartocss_LIBS_DIR "${PROJECT_SOURCE_DIR}/../../libs-external")

file(GLOB cartocss_SRC_FILES "${cartocss_SRC_DIR}/*.cpp" "${cartocss_SRC_DIR}/*.h")

set(cartocss_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src" PARENT_SCOPE)

if(SINGLE_LIBRARY)
  set(cartocss_SRC_FILES ${cartocss_SRC_FILES} PARENT_SCOPE)
  add_library(cartocss INTERFACE)
else()
  include_directories(
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/../mapnikvt/src"
    "${PROJECT_SOURCE_DIR}/../vt/src"
    "${cartocss_LIBS_DIR}/picojson"
    "${cartocss_LIBS_DIR}/cglib"
    "${cartocss_LIBS_DIR}/stdext"
  )
  add_library(cartocss OBJECT ${cartocss_SRC_FILES})
endif()
