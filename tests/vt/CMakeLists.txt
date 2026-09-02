# vt without the renderer, and without freetype or harfbuzz. Most of vt is header-only here; the
# compiled sources are Label.cpp and, for the line tesselation, TileLayerBuilder.cpp and the six
# TUs it names. That chain wants tess2 and bidi but NO font: TextFormatter reaches Font through
# its interface only, so freetype and harfbuzz - the weight ../README.md keeps out - stay out.
# Anything reaching GL still belongs behind a device check instead.
file(GLOB TESS2_SOURCES "${SDK_BASE_DIR}/libs-external/tess2/source/*.c")
file(GLOB BIDI_SOURCES "${SDK_BASE_DIR}/libs-external/bidi/bidi.c" "${SDK_BASE_DIR}/libs-external/bidi/ucdn.c")

add_executable(vt_tests
  VtTest.cpp
  PlateBitmapTest.cpp
  LineLabelTest.cpp
  ExtrusionCornerTest.cpp
  ExtrusionBevelTest.cpp
  LineJoinTest.cpp
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/Label.cpp"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/TileLayerBuilder.cpp"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/TileTransformer.cpp"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/PolygonTesselator.cpp"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/TextFormatter.cpp"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/GlyphMap.cpp"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/StrokeMap.cpp"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt/BitmapManager.cpp"
  ${TESS2_SOURCES}
  ${BIDI_SOURCES})

target_include_directories(vt_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  # TestCheck.h is the whole framework and lives with the api tests.
  "${CMAKE_CURRENT_SOURCE_DIR}/../api"
  "${SDK_BASE_DIR}/libs-massif/vt/src/vt"
  "${SDK_BASE_DIR}/libs-massif/vt/src"
  "${SDK_BASE_DIR}/libs-external/cglib"
  "${SDK_BASE_DIR}/libs-external/stdext"
  "${SDK_BASE_DIR}/libs-external/tess2/include"
  "${SDK_BASE_DIR}/libs-external/bidi"
  # stdext/unistring.h, reached from TextFormatter.cpp.
  "${SDK_BASE_DIR}/libs-external/utf8/source"
  # vt uses boost only for boost::math::constants::pi - see ../../BUILDING.md for the symlink.
  "${SDK_BASE_DIR}/libs-external/boost")

add_test(NAME vt COMMAND vt_tests)
