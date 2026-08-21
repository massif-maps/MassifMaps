cmake_minimum_required(VERSION 3.5)
project(massif)

enable_language( C CXX ASM )

# General options
option(SINGLE_LIBRARY "Compile as single library" OFF)

if(APPLE)
  option(INCLUDE_OBJC "Include ObjC code on iOS" OFF)
  option(SHARED_LIBRARY "Build as shared library on iOS" OFF)
endif()

# Directories
set(SDK_BASE_DIR "${PROJECT_SOURCE_DIR}/../..")
set(SDK_SRC_DIR "${SDK_BASE_DIR}/all/native")
set(SDK_MASSIF_LIBS_DIR "${SDK_BASE_DIR}/libs-massif" )
set(SDK_EXTERNAL_LIBS_DIR "${SDK_BASE_DIR}/libs-external")

# General platform-specific flags
if(WIN32 OR ANDROID)
  set(SDK_LIBRARY_TYPE SHARED)
elseif(SHARED_LIBRARY)
  set(SDK_LIBRARY_TYPE SHARED)
elseif(NOT SHARED_LIBRARY)
  set(SDK_LIBRARY_TYPE STATIC)
endif()

# C++ compiler flags
add_definitions("${SDK_CPP_DEFINES}" "-DBOOST_ALL_NO_LIB" "-DHAVE_ZSTD")

if(CMAKE_BUILD_TYPE MATCHES "Release|RELEASE")
  add_definitions("-DNDEBUG")
endif()

if(WIN32)
  add_definitions("-DNOMINMAX -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -DGL_GLEXT_PROTOTYPES")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4244")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244 /std:c++20 /Zc:__cplusplus /EHs /GR")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Os /GL")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Os /GL")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /WINMD vccorlib.lib")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")

  set(CMAKE_VS_WINRT_EXTENSIONS ON)
endif(WIN32)

if(APPLE AND (NOT IOS))
  set(SDK_MACCATALYST TRUE)
  execute_process(COMMAND "xcodebuild" "-version" "-sdk" "macosx" "Path" OUTPUT_VARIABLE OSX_SDK_PATH ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

  add_compile_options("-target" "${SDK_IOS_ARCH}-apple-ios-13.0-macabi" "-iframework" "${OSX_SDK_PATH}/System/iOSSupport/System/Library/Frameworks")
  add_compile_options("-miphoneos-version-min=13.0")

  set(CMAKE_XCODE_EFFECTIVE_PLATFORMS "-maccatalyst")
endif()

if(APPLE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc -fmodules -fvisibility=hidden -ffunction-sections -fdata-sections")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc -fmodules -stdlib=libc++ -std=c++20 -ftemplate-depth=1024 -fexceptions -frtti -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fno-aligned-allocation -Wno-unqualified-std-cast-call -Wno-shorten-64-to-32")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Oz")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Oz")
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -ObjC")

  set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_CODE_COVERAGE NO)
  set(CMAKE_XCODE_ATTRIBUTE_GCC_INSTRUMENT_PROGRAM_FLOW_ARCS NO)

  if(SDK_CPP_DEFINES MATCHES ".*_MASSIF_USE_METALANGLE.*")
    if(SDK_MACCATALYST)
      # No prelink here (see below), so libangle is merged by the libtool step instead. libtool
      # copies archive members without checking their platform, which ld -r refuses to do.
      set(CMAKE_XCODE_ATTRIBUTE_OTHER_LIBTOOLFLAGS "${SDK_EXTERNAL_LIBS_DIR}/angle-metal/${SDK_IOS_BASEARCH}/libangle.a")
    else()
      set(CMAKE_XCODE_ATTRIBUTE_PRELINK_LIBS "${SDK_EXTERNAL_LIBS_DIR}/angle-metal/${SDK_IOS_BASEARCH}/libangle.a")
    endif()
  endif()

  if(SDK_DEV_TEAM)
    set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${SDK_DEV_TEAM})
  endif()

  # LTO used to be shared-framework only, so the static framework - what actually ships - was the
  # one build without it. It is safe here because the Release static library is prelinked into a
  # single master object (GENERATE_MASTER_OBJECT_FILE below): the ld -r that produces it runs the
  # LTO codegen, so the archive member we ship is native code, not bitcode that would then need a
  # compatible toolchain in every consuming app.
  #
  # Mac Catalyst gets neither. This is a macOS Xcode project with -target ...-macabi forced onto the
  # compiles, so the prelink runs ld -r for macOS and refuses every real Mach-O input stamped Mac
  # Catalyst - the prebuilt libangle.a, and any object not turned into bitcode by LTO. Without the
  # prelink there is nothing to run the LTO codegen, so LTO has to go with it. See
  # docs/maintenance/mac-catalyst.md.
  if(NOT SDK_MACCATALYST)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto=full")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=full")
  endif()

  if(CMAKE_BUILD_TYPE MATCHES "Release|RELEASE")
    set(CMAKE_XCODE_ATTRIBUTE_DEPLOYMENT_POSTPROCESSING YES)
    set(CMAKE_XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING YES)
    set(CMAKE_XCODE_ATTRIBUTE_SEPARATE_STRIP YES)
    set(CMAKE_XCODE_ATTRIBUTE_STRIP_STYLE "non-global")
    if(NOT SDK_MACCATALYST)
      set(CMAKE_XCODE_ATTRIBUTE_GENERATE_MASTER_OBJECT_FILE YES)
    endif()
  endif()
endif(APPLE)

if(ANDROID)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden -ffunction-sections -fdata-sections")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -ftemplate-depth=1024 -fexceptions -frtti -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Oz -flto=thin -fomit-frame-pointer")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Oz -flto=thin -fomit-frame-pointer")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -s -fuse-ld=lld -Wl,-plugin-opt=O3 -Wl,--gc-sections -Wl,-icf=all -Wl,--as-needed -Wl,--build-id -Wl,--version-script=${PROJECT_SOURCE_DIR}/../android/version-script")
  if(WIN32)
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -Wl,-threads=4")
  endif()
endif(ANDROID)

# Make common libraries available to all subprojects
include_directories("${SDK_EXTERNAL_LIBS_DIR}/boost")
include_directories("${SDK_EXTERNAL_LIBS_DIR}/boost/libs/any/include")

if(APPLE)
  # include_directories("${SDK_BASE_DIR}/ios/stdlib")
  if(SDK_CPP_DEFINES MATCHES ".*_MASSIF_USE_METALANGLE.*")
    include_directories("${SDK_EXTERNAL_LIBS_DIR}/angle-metal/include")
  endif()
endif(APPLE)

if(WIN32)
  include_directories("${SDK_EXTERNAL_LIBS_DIR}/angle-uwp/include" "${SDK_EXTERNAL_LIBS_DIR}/zlib/config" "${SDK_EXTERNAL_LIBS_DIR}/zlib/zlib")
endif(WIN32)

# Subprojects
set(SDK_MASSIF_SUBPROJECTS vt mapnikvt mbvtbuilder cartocss nml)
set(SDK_EXTERNAL_SUBPROJECTS botan pugixml bidi freetype harfbuzz brotli zstd miniz libjpeg libpng libwebp tess2 pvrt rg_etc1 mlt)

if(WIN32)
  list(APPEND SDK_EXTERNAL_SUBPROJECTS "zlib")
endif(WIN32)

if(NOT (WIN32 OR APPLE OR ANDROID))
  list(APPEND SDK_EXTERNAL_SUBPROJECTS "pion")
endif()

if(INCLUDE_GEOCODING)
  list(APPEND SDK_MASSIF_SUBPROJECTS "geocoding")
endif(INCLUDE_GEOCODING)

if(INCLUDE_ROUTING)
  list(APPEND SDK_MASSIF_SUBPROJECTS "osrm" "sgre")
endif(INCLUDE_ROUTING)  

if(INCLUDE_SQLITE)
  list(APPEND SDK_EXTERNAL_SUBPROJECTS "sqlite" "sqlite3pp")
endif(INCLUDE_SQLITE)

if(INCLUDE_VALHALLA)
  # date is headers only now (valhalla/third_party/date); tz comes from valhalla's tz_alt.cpp
  list(APPEND SDK_EXTERNAL_SUBPROJECTS "protobuf" "valhalla")
  # ValhallaRoutingProxy only calls route(), trace_attributes() and narrate(), so the matrix,
  # isochrone, locate, height, expansion, status and optimized-route actions are dead weight in the
  # SDK. Declared here so this default wins over the ON default in the valhalla subproject.
  option(VALHALLA_SERVICES "Build the Valhalla service actions beyond routing and map matching" OFF)
endif(INCLUDE_VALHALLA)

foreach(SDK_SUBPROJECT IN ITEMS ${SDK_MASSIF_SUBPROJECTS})
  add_subdirectory("${SDK_MASSIF_LIBS_DIR}/${SDK_SUBPROJECT}" ${SDK_SUBPROJECT})
endforeach()

foreach(SDK_SUBPROJECT IN ITEMS ${SDK_EXTERNAL_SUBPROJECTS})
  add_subdirectory("${SDK_EXTERNAL_LIBS_DIR}/${SDK_SUBPROJECT}" ${SDK_SUBPROJECT})
endforeach()

set(SDK_SUBPROJECTS "${SDK_MASSIF_SUBPROJECTS}" "${SDK_EXTERNAL_SUBPROJECTS}")

# When in single library mode, apply compilation flags to individual projects.
if(SINGLE_LIBRARY)
  foreach(SDK_SUBPROJECT IN ITEMS ${SDK_SUBPROJECTS})
    set(SUBPROJECT_FLAGS "${${SDK_SUBPROJECT}_FLAGS}")
    if(CMAKE_BUILD_TYPE MATCHES "Release|RELEASE")
      if(ANDROID OR APPLE)
        if(SDK_SUBPROJECT MATCHES "^(zstd)$" AND APPLE)
            set(SUBPROJECT_FLAGS ${SUBPROJECT_FLAGS} "-fno-modules")
        endif()
        if(SDK_SUBPROJECT MATCHES "^(vt|mapnikvt|cartocss|freetype|tess2|zlib|brotli|zstd)$")
          set(SUBPROJECT_FLAGS ${SUBPROJECT_FLAGS} "-O2")
        else()
          set(SUBPROJECT_FLAGS ${SUBPROJECT_FLAGS} "-Oz")
        endif()
      elseif(WIN32)
        if(SDK_SUBPROJECT MATCHES "^(vt|mapnikvt|cartocss|freetype|tess2|zlib|brotli|zstd)$")
          set(SUBPROJECT_FLAGS ${SUBPROJECT_FLAGS} "/Ox")
        else()
          set(SUBPROJECT_FLAGS ${SUBPROJECT_FLAGS} "/Os")
        endif()
      endif()
    endif()
    if(SUBPROJECT_FLAGS)
      set_source_files_properties(${${SDK_SUBPROJECT}_SRC_FILES} PROPERTIES COMPILE_OPTIONS "${SUBPROJECT_FLAGS}")
    endif()
  endforeach()
endif(SINGLE_LIBRARY)

# Source files and properties
# CONFIGURE_DEPENDS: without it the glob is evaluated only at configure time, so a newly added
# source file is silently left out of the build and shows up as undefined symbols at link.
file(GLOB SDK_SRC_FILES CONFIGURE_DEPENDS
  "${SDK_SRC_DIR}/*/*.cpp"
  "${SDK_SRC_DIR}/*/*/*.cpp"
  "${SDK_SRC_DIR}/*/*.h"
  "${SDK_SRC_DIR}/*/*/*.h"
)
set_source_files_properties("${SDK_SRC_DIR}/utils/PlatformUtils.cpp" PROPERTIES COMPILE_OPTIONS "-D_MASSIF_MOBILE_SDK_PLATFORM=\"${SDK_PLATFORM}\";-D_MASSIF_MOBILE_SDK_VERSION=\"${SDK_VERSION}\"")

# Release builds the whole SDK at -Oz, which is right for the API surface but not for the code
# the frame budget is actually spent in. Build the render and tile paths for speed, same as the
# vt/mapnikvt/cartocss subprojects above; everything else (routing, geocoding, search, package
# manager, network, ui) stays at -Oz.
if(CMAKE_BUILD_TYPE MATCHES "Release|RELEASE")
  set(SDK_SPEED_DIRS renderers layers terrain graphics geometry projections vectortiles vectorelements rastertiles datasources)
  foreach(SDK_SPEED_DIR IN ITEMS ${SDK_SPEED_DIRS})
    file(GLOB SDK_SPEED_SRC_FILES CONFIGURE_DEPENDS
      "${SDK_SRC_DIR}/${SDK_SPEED_DIR}/*.cpp"
      "${SDK_SRC_DIR}/${SDK_SPEED_DIR}/*/*.cpp"
    )
    if(WIN32)
      set_source_files_properties(${SDK_SPEED_SRC_FILES} PROPERTIES COMPILE_OPTIONS "/Ox")
    else()
      set_source_files_properties(${SDK_SPEED_SRC_FILES} PROPERTIES COMPILE_OPTIONS "-O2")
    endif()
  endforeach()
endif()

if(WIN32 OR APPLE OR ANDROID)
  list(REMOVE_ITEM SDK_SRC_FILES "${SDK_SRC_DIR}/network/HTTPClientPionImpl.cpp")
endif()

if(ANDROID)
  file(GLOB SDK_ANDROID_SRC_FILES CONFIGURE_DEPENDS
    "${WRAPPER_DIR}/*.cpp"
    "${WRAPPER_DIR}/*.h"
    "${SDK_BASE_DIR}/android/native/*/*.cpp"
    "${SDK_BASE_DIR}/android/native/*/*.h"
    "${SDK_BASE_DIR}/android/native/*/*/*.cpp"
    "${SDK_BASE_DIR}/android/native/*/*/*.h"
  )
  set(SDK_SRC_FILES "${SDK_SRC_FILES}" "${SDK_ANDROID_SRC_FILES}")
elseif(APPLE)
  if(INCLUDE_OBJC)
    file(GLOB SDK_OBJC_SRC_FILES CONFIGURE_DEPENDS
      "${SDK_BASE_DIR}/ios/objc/*/*.cpp"
      "${SDK_BASE_DIR}/ios/objc/*/*.mm"
      "${SDK_BASE_DIR}/ios/objc/*/*.h"
      "${SDK_BASE_DIR}/ios/objc/*.h"
    )
    set(SDK_SRC_FILES "${SDK_SRC_FILES}" "${SDK_OBJC_SRC_FILES}")
  endif(INCLUDE_OBJC)
  file(GLOB SDK_IOS_SRC_FILES CONFIGURE_DEPENDS
    "${WRAPPER_DIR}/*.cpp"
    "${WRAPPER_DIR}/*.mm"
    "${WRAPPER_DIR}/*.h"
    "${SDK_BASE_DIR}/ios/native/*/*.cpp"
    "${SDK_BASE_DIR}/ios/native/*/*.mm"
    "${SDK_BASE_DIR}/ios/native/*/*.h"
  )
  set(SDK_SRC_FILES "${SDK_SRC_FILES}" "${SDK_IOS_SRC_FILES}")
elseif(WIN32)
  file(GLOB SDK_WINRT_SRC_FILES CONFIGURE_DEPENDS
    "${WRAPPER_DIR}/*.cpp"
    "${WRAPPER_DIR}/*.h"
    "${SDK_BASE_DIR}/winphone/native/*/*.cpp"
    "${SDK_BASE_DIR}/winphone/native/*/*.h"
    "${SDK_BASE_DIR}/winphone/native/*/*/*.cpp"
    "${SDK_BASE_DIR}/winphone/native/*/*/*.h"
  )
  set(SDK_SRC_FILES "${SDK_SRC_FILES}" "${SDK_WINRT_SRC_FILES}")

  foreach(WINRT_SRC_FILE IN ITEMS "utils/AssetUtils.cpp" "utils/PlatformUtils.cpp" "utils/EGLContextWrapper.cpp" "components/Task.cpp")
    set_source_files_properties("${SDK_BASE_DIR}/winphone/native/${WINRT_SRC_FILE}" PROPERTIES COMPILE_OPTIONS "/ZW")
  endforeach()
endif()

# Group wrapper files into different groups, to reduce clutter
source_group("Wrapper Sources" REGULAR_EXPRESSION ".*_wrap.cpp")
source_group("Wrapper Headers" REGULAR_EXPRESSION ".*_wrap.h")

# The facade API's property table is derived from the Swig attribute macros, so it is generated
# rather than checked in: adding a setter adds a path with no second step to remember.
#
# Generated at BUILD time, not configure time. AGP decides whether to re-run CMake from its own
# hash of the configuration, so it skips a reconfigure when only a module changed - and a stale
# table shows up as a property that silently does not exist. A build rule with the modules as
# DEPENDS is checked by ninja on every build instead.
find_program(SDK_PYTHON NAMES python3 python)
if(NOT SDK_PYTHON)
  message(FATAL_ERROR "python3 is required to generate the API property table")
endif()
# all/modules only. The platform module sets are glue - iOS BitmapUtils/AssetUtils, Android's
# asset package - and their headers are Objective-C on Apple, which a C++ translation unit cannot
# include. They are also not the cross-platform surface the facade is about.
set(SDK_API_SOURCEDIR "${SDK_BASE_DIR}/all/modules")
set(SDK_API_CPPDIR "${SDK_SRC_DIR}")
file(GLOB_RECURSE SDK_MODULE_FILES "${SDK_API_SOURCEDIR}/*.i")
# This build's own support defines, not a profile name: the thunks have to be emitted for exactly
# the classes being compiled, or a lite build fails to link.
string(REGEX MATCHALL "_MASSIF_[A-Za-z0-9_]+" SDK_API_DEFINE_LIST "${SDK_CPP_DEFINES}")
list(JOIN SDK_API_DEFINE_LIST "," SDK_API_DEFINES)
set(SDK_API_TABLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/api")
set(SDK_API_TABLE_FILES "${SDK_API_TABLE_DIR}/PropertyTable.inc" "${SDK_API_TABLE_DIR}/PropertyAccessors.inc")
add_custom_command(
  OUTPUT ${SDK_API_TABLE_FILES}
  COMMAND "${SDK_PYTHON}" "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
          # Comma, not semicolon: ninja runs this through a shell, which would split on one.
          "--sourcedir" "${SDK_API_SOURCEDIR}"
          "--defines" "${SDK_API_DEFINES}"
          # The base-class chain is read from the headers, so the generator needs the same
          # include roots the compile does.
          "--cppdir" "${SDK_API_CPPDIR}"
          "--outdir" "${SDK_API_TABLE_DIR}"
  DEPENDS ${SDK_MODULE_FILES} "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
  COMMENT "Generating the API property table")
add_custom_target(massif_api_tables DEPENDS ${SDK_API_TABLE_FILES})
set_source_files_properties("${SDK_SRC_DIR}/api/PropertyTable.cpp" PROPERTIES OBJECT_DEPENDS "${SDK_API_TABLE_FILES}")

# Include directories for main SDK project
include_directories("${SDK_SRC_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated")

foreach(SDK_HEADER_LIB IN ITEMS "cglib" "stdext" "picojson" "pbf" "rapidjson/include" "tinyformat" "nanosvg/src" "utf8/source" "geojsonvt/geojson-vt-cpp/include" "geojsonvt/geometry.hpp/include" "geojsonvt/variant/include")
  include_directories("${SDK_EXTERNAL_LIBS_DIR}/${SDK_HEADER_LIB}")
endforeach()

foreach(SDK_SUBPROJECT IN ITEMS ${SDK_SUBPROJECTS})
  set(SUBPROJECT_INCLUDE_DIRS ${${SDK_SUBPROJECT}_INCLUDE_DIRS})
  if(SUBPROJECT_INCLUDE_DIRS)
    include_directories("${SUBPROJECT_INCLUDE_DIRS}")
  endif()
endforeach()

if(ANDROID)
  include_directories("${SDK_BASE_DIR}/android/native")
elseif(APPLE)
  include_directories("${SDK_BASE_DIR}/ios/native")
  if(INCLUDE_OBJC)
    include_directories("${SDK_BASE_DIR}/generated/ios-objc/proxies")
    include_directories("${SDK_BASE_DIR}/generated/ios-objc/wrappers")
    include_directories("${SDK_BASE_DIR}/ios/objc")
  endif(INCLUDE_OBJC)
elseif(WIN32)
  include_directories("${SDK_BASE_DIR}/winphone/native")
endif()

# Linking
set(SDK_OBJECTS "")
if(SINGLE_LIBRARY)
  foreach(SDK_SUBPROJECT IN ITEMS ${SDK_SUBPROJECTS})
    set(SDK_SRC_FILES "${SDK_SRC_FILES}" "${${SDK_SUBPROJECT}_SRC_FILES}")
  endforeach()
else()
  foreach(SDK_SUBPROJECT IN ITEMS ${SDK_SUBPROJECTS})
    set(SDK_OBJECTS "${SDK_OBJECTS}" "$<TARGET_OBJECTS:${SDK_SUBPROJECT}>")
  endforeach()
endif()

add_library(massif ${SDK_LIBRARY_TYPE} ${SDK_SRC_FILES} ${SDK_OBJECTS})

# The generated tables exist before anything that includes them compiles.
add_dependencies(massif massif_api_tables)

if(ANDROID)
  # GLESv3, not GLESv2: the ES 3.0 core entry points (glGenVertexArrays, glInvalidateFramebuffer,
  # ...) are only exported by libGLESv3.so. It is a superset, so it replaces GLESv2 rather than
  # joining it, and it exists on every device at minSdk 21.
  target_link_libraries(massif EGL GLESv3 z log android jnigraphics)
elseif(APPLE)
  target_link_libraries(massif "-framework Foundation" "-framework CoreGraphics" "-framework CoreText" "-framework CFNetwork" "-framework UIKit")
  if(SDK_CPP_DEFINES MATCHES ".*_MASSIF_USE_METALANGLE.*")
    target_link_libraries(massif "-weak_framework OpenGLES" "-weak_framework Metal" "-framework QuartzCore")
  else()
    target_link_libraries(massif "-framework OpenGLES" "-framework GLKit")
  endif()
elseif(WIN32)
  target_link_libraries(massif "${SDK_EXTERNAL_LIBS_DIR}/angle-uwp/${SDK_WINPHONE_ARCH}/libEGL.dll.lib" "${SDK_EXTERNAL_LIBS_DIR}/angle-uwp/${SDK_WINPHONE_ARCH}/libGLESv2.dll.lib")
  target_link_libraries(massif "msxml6.lib" "d3d11.lib" "dwrite.lib" "d2d1.lib")
endif()
