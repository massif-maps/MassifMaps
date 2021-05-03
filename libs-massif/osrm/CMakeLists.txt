project(osrm)

set(osrm_SRC_DIR "${PROJECT_SOURCE_DIR}/src/osrm")
set(osrm_LIBS_DIR "${PROJECT_SOURCE_DIR}/../../libs-external")

file(GLOB osrm_SRC_FILES "${osrm_SRC_DIR}/*.cpp" "${osrm_SRC_DIR}/*.h")

set(osrm_FLAGS "")
if(ANDROID OR IOS)
  set(osrm_FLAGS ${osrm_FLAGS} "-fno-math-errno" "-fno-trapping-math")
endif()

set(osrm_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src" PARENT_SCOPE)

if(SINGLE_LIBRARY)
  set(osrm_SRC_FILES ${osrm_SRC_FILES} PARENT_SCOPE)
  set(osrm_FLAGS ${osrm_FLAGS} PARENT_SCOPE)
  add_library(osrm INTERFACE)
else()
  add_compile_options(${osrm_FLAGS})
  include_directories(
    "${PROJECT_SOURCE_DIR}/src"
    "${osrm_LIBS_DIR}/utf8/source"
    "${osrm_LIBS_DIR}/cglib"
    "${osrm_LIBS_DIR}/stdext"
  )
  add_library(osrm OBJECT ${osrm_SRC_FILES})
endif()
