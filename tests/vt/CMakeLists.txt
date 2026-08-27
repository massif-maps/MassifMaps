# vt without the renderer, and without freetype or harfbuzz: only the HEADER-ONLY parts of vt are
# covered here, so nothing of vt is compiled in. Anything reached through a .cpp of vt drags the
# tile builders and the font stack in - that case belongs behind a device check instead.
add_executable(vt_tests
  VtTest.cpp
  PlateBitmapTest.cpp)

target_include_directories(vt_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  # TestCheck.h is the whole framework and lives with the api tests.
  "${CMAKE_CURRENT_SOURCE_DIR}/../api"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt")

add_test(NAME vt COMMAND vt_tests)
