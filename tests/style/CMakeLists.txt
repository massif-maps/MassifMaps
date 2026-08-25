# mapnikvt without the renderer. Only the TUs the style MODEL needs are listed: the symbolizer
# implementations pull vt's tile builders in, and with them freetype and harfbuzz, which is the
# weight ../README.md keeps out of this suite. Symbolizer.cpp itself is the property binding only.
set(STYLE_SOURCES
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/LayerConfigResolver.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/Symbolizer.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/Map.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/Style.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/Rule.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/Expression.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/ExpressionContext.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/ExpressionUtils.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/Predicate.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/PredicateUtils.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/TransformUtils.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/Value.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/Feature.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/ParserUtils.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/ParseTables.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/CSSColorParser.cpp"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src/mapnikvt/StringUtils.cpp"
)

add_executable(style_tests
  StyleTest.cpp
  LayerConfigTest.cpp
  ${STYLE_SOURCES})

target_include_directories(style_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  # TestCheck.h is the whole framework and lives with the api tests.
  "${CMAKE_CURRENT_SOURCE_DIR}/../api"
  "${SDK_BASE_DIR}/libs-massif/mapnikvt/src"
  # vt headers only - no vt object is linked.
  "${SDK_BASE_DIR}/libs-massif/vt/src"
  "${SDK_BASE_DIR}/libs-external/cglib"
  "${SDK_BASE_DIR}/libs-external/stdext"
  # stdext/unistring.h, reached from StringUtils.cpp.
  "${SDK_BASE_DIR}/libs-external/utf8/source"
  "${SDK_BASE_DIR}/libs-external/boost"
  "${SDK_BASE_DIR}/libs-external/picojson"
  "${SDK_BASE_DIR}/libs-external/tinyformat")

add_test(NAME style COMMAND style_tests)
