project(mapnikvt)

set(mapnikvt_SRC_DIR "${PROJECT_SOURCE_DIR}/src/mapnikvt")
set(mapnikvt_LIBS_DIR "${PROJECT_SOURCE_DIR}/../../libs-external")

set(mapnikvt_FLAGS "")
if(ANDROID OR APPLE)
  set(mapnikvt_FLAGS ${mapnikvt_FLAGS} "-fno-math-errno")
endif()

file(GLOB mapnikvt_SRC_FILES "${mapnikvt_SRC_DIR}/*.cpp" "${mapnikvt_SRC_DIR}/*.h" "${mapnikvt_SRC_DIR}/MBVTPackage/*.cc" "${mapnikvt_SRC_DIR}/MBVTPackage/*.h")

set(mapnikvt_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/src" PARENT_SCOPE)

if(SINGLE_LIBRARY)
  set(mapnikvt_SRC_FILES ${mapnikvt_SRC_FILES} PARENT_SCOPE)
  set(mapnikvt_FLAGS ${mapnikvt_FLAGS} PARENT_SCOPE)
  add_library(mapnikvt INTERFACE)
else()
  add_compile_options(${mapnikvt_FLAGS})
  include_directories(
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/../vt/src"
    "${mapnikvt_LIBS_DIR}/pbf"
    "${mapnikvt_LIBS_DIR}/pugixml/pugixml/src"
    "${mapnikvt_LIBS_DIR}/rapidjson/include"
    "${mapnikvt_LIBS_DIR}/utf8/source"
    "${mapnikvt_LIBS_DIR}/cglib"
    "${mapnikvt_LIBS_DIR}/stdext"
    "${mapnikvt_LIBS_DIR}/mlt/mlt/cpp/include"
  )
  # STATIC lets the linker leave out whole objects nothing references. The SDK build cannot use
  # it: it aggregates these with $<TARGET_OBJECTS:mapnikvt>.
  if(STATIC_LIBRARIES)
    add_library(mapnikvt STATIC ${mapnikvt_SRC_FILES})
  else()
    add_library(mapnikvt OBJECT ${mapnikvt_SRC_FILES})
  endif()
endif()
