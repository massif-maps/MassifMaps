project(geocoding)

set(geocoding_SRC_DIR "${PROJECT_SOURCE_DIR}/src/geocoding")
set(geocoding_LIBS_DIR "${PROJECT_SOURCE_DIR}/../../libs-external")

file(GLOB geocoding_SRC_FILES "${geocoding_SRC_DIR}/*.cpp" "${geocoding_SRC_DIR}/*.h")

set(geocoding_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src" PARENT_SCOPE)

if(SINGLE_LIBRARY)
  set(geocoding_SRC_FILES ${geocoding_SRC_FILES} PARENT_SCOPE)
  add_library(geocoding INTERFACE)
else()
  include_directories(
    "${PROJECT_SOURCE_DIR}/src"
    "${geocoding_LIBS_DIR}/sqlite"
    "${geocoding_LIBS_DIR}/sqlite3pp"
    "${geocoding_LIBS_DIR}/utf8/source"
    "${geocoding_LIBS_DIR}/cglib"
    "${geocoding_LIBS_DIR}/stdext"
  )
  add_library(geocoding OBJECT ${geocoding_SRC_FILES})
endif()
