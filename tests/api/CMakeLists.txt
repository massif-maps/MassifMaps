# The property table takes the address of every accessor thunk, so a table generated over the whole
# SDK would need the whole SDK to link. These tests generate a REDUCED table from an explicit list
# of modules instead, which keeps the link to a handful of files - and exercises the generator.
set(TEST_MODULES
  "${SDK_BASE_DIR}/all/modules/components/FogOptions.i"
  "${SDK_BASE_DIR}/all/modules/geometry/Feature.i"
  "${SDK_BASE_DIR}/all/modules/geometry/Geometry.i"
  "${SDK_BASE_DIR}/all/modules/ui/VectorTileClickInfo.i"
  "${SDK_BASE_DIR}/all/modules/geometry/PointGeometry.i"
  # For the projection flags: Projection declares a position (bounds), and the GeoJSON writer is
  # the lightest class that declares a projection of its own.
  "${SDK_BASE_DIR}/all/modules/projections/Projection.i"
  "${SDK_BASE_DIR}/all/modules/geometry/GeoJSONGeometryWriter.i"
  # For call/getData: TileData is the lightest class with a BinaryData property.
  "${SDK_BASE_DIR}/all/modules/datasources/components/TileData.i"
  "${SDK_BASE_DIR}/all/modules/core/BinaryData.i"
)

# The SDK sources those modules' classes need. Keep this short: a case that needs a long list is a
# case that belongs behind a device check.
set(TEST_SDK_SOURCES
  "${SDK_BASE_DIR}/all/native/api/Context.cpp"
  "${SDK_BASE_DIR}/all/native/api/EventBus.cpp"
  "${SDK_BASE_DIR}/all/native/api/StructCodec.cpp"
  "${SDK_BASE_DIR}/all/native/api/PropertyTable.cpp"
  "${SDK_BASE_DIR}/all/native/api/Projections.cpp"
  # Methods.cpp is the registry; MethodImpls.cpp holds the SDK's own methods and is deliberately
  # NOT here - it needs TileDataSource and the hillshade layer. The tests register their own.
  "${SDK_BASE_DIR}/all/native/api/Methods.cpp"
  "${SDK_BASE_DIR}/all/native/core/BinaryData.cpp"
  "${SDK_BASE_DIR}/all/native/datasources/components/TileData.cpp"
  "${SDK_BASE_DIR}/all/native/projections/Projection.cpp"
  "${SDK_BASE_DIR}/all/native/projections/EPSG3857.cpp"
  "${SDK_BASE_DIR}/all/native/projections/EPSG4326.cpp"
  # Spec.cpp holds create() and the registry; the built-in factories live in SpecFactories.cpp,
  # which is deliberately NOT here - linking it would need every source and layer constructor.
  "${SDK_BASE_DIR}/all/native/api/Spec.cpp"
  "${SDK_BASE_DIR}/all/native/core/Variant.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapPos.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapVec.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapRange.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapBounds.cpp"
  "${SDK_BASE_DIR}/all/native/core/ScreenPos.cpp"
  "${SDK_BASE_DIR}/all/native/components/FogOptions.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/Feature.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/VectorTileFeature.cpp"
  "${SDK_BASE_DIR}/all/native/ui/VectorTileClickInfo.cpp"
  "${SDK_BASE_DIR}/all/native/ui/ClickInfo.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapTile.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/GeoJSONGeometryWriter.cpp"
  # The writer handles every geometry type, so they all come with it. They are light - no
  # renderer, no data sources - which is why this stays inside the rule in ../README.md.
  "${SDK_BASE_DIR}/all/native/geometry/FeatureCollection.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/LineGeometry.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/MultiGeometry.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/MultiLineGeometry.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/MultiPointGeometry.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/MultiPolygonGeometry.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/PolygonGeometry.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/PointGeometry.cpp"
  "${SDK_BASE_DIR}/all/native/graphics/Color.cpp"
  "${SDK_BASE_DIR}/all/native/utils/Log.cpp"
  "${SDK_BASE_DIR}/all/native/utils/GeomUtils.cpp"
  "${SDK_BASE_DIR}/all/native/utils/GeneralUtils.cpp"
  "${SDK_BASE_DIR}/all/native/utils/Const.cpp"
)

set(TEST_TABLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/api")
list(JOIN TEST_MODULES "," TEST_MODULE_LIST)

find_program(TEST_PYTHON NAMES python3 python REQUIRED)

add_custom_command(
  OUTPUT "${TEST_TABLE_DIR}/PropertyTable.inc" "${TEST_TABLE_DIR}/PropertyAccessors.inc"
  COMMAND "${TEST_PYTHON}" "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
          "--modules" "${TEST_MODULE_LIST}"
          "--cppdir" "${SDK_BASE_DIR}/all/native"
          "--outdir" "${TEST_TABLE_DIR}"
  DEPENDS ${TEST_MODULES} "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
  WORKING_DIRECTORY "${SDK_BASE_DIR}/scripts"
  COMMENT "Generating the reduced API property table for tests"
  VERBATIM)

add_executable(api_tests
  ApiTest.cpp
  EventTest.cpp
  StructCodecTest.cpp
  ProjectionTest.cpp
  MethodTest.cpp
  "${TEST_TABLE_DIR}/PropertyTable.inc"
  ${TEST_SDK_SOURCES})

set_source_files_properties("${TEST_TABLE_DIR}/PropertyTable.inc" PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(api_tests PRIVATE
  "${SDK_BASE_DIR}/all/native"
  "${TEST_TABLE_DIR}/.."
  "${SDK_BASE_DIR}/libs-external/cglib"
  "${SDK_BASE_DIR}/libs-external/stdext"
  "${SDK_BASE_DIR}/libs-external/picojson"
  "${SDK_BASE_DIR}/libs-external/rapidjson/include"
  "${SDK_BASE_DIR}/libs-external/tinyformat")

add_test(NAME api COMMAND api_tests)
