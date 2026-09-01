# vt without the renderer, and without freetype or harfbuzz. Most of vt is header-only here; the
# one compiled source is Label.cpp, which reaches no font, no tile builder and no GL. Anything
# else reached through a .cpp of vt drags the tile builders and the font stack in - that case
# belongs behind a device check instead.
add_executable(vt_tests
  VtTest.cpp
  PlateBitmapTest.cpp
  LineLabelTest.cpp
  ExtrusionCornerTest.cpp
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/Label.cpp")

target_include_directories(vt_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  # TestCheck.h is the whole framework and lives with the api tests.
  "${CMAKE_CURRENT_SOURCE_DIR}/../api"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt"
  "${SDK_BASE_DIR}/libs-massif/vt/src"
  "${SDK_BASE_DIR}/libs-external/cglib"
  "${SDK_BASE_DIR}/libs-external/stdext"
  # vt uses boost only for boost::math::constants::pi - see ../../BUILDING.md for the symlink.
  "${SDK_BASE_DIR}/libs-external/boost")

add_test(NAME vt COMMAND vt_tests)
