# The property table takes the address of every accessor thunk, so a table generated over the whole
# SDK would need the whole SDK to link. These tests generate a REDUCED table from an explicit list
# of modules instead, which keeps the link to a handful of files - and exercises the generator.
set(TEST_MODULES
  "${SDK_BASE_DIR}/all/modules/components/FogOptions.i"
  "${SDK_BASE_DIR}/all/modules/geometry/Feature.i"
  "${SDK_BASE_DIR}/all/modules/geometry/Geometry.i"
  "${SDK_BASE_DIR}/all/modules/ui/VectorTileClickInfo.i"
  "${SDK_BASE_DIR}/all/modules/geometry/PointGeometry.i"
  # For the collection channel: a search result is a VectorTileFeatureCollection, and only the
  # subclass carries layerName and distance.
  "${SDK_BASE_DIR}/all/modules/geometry/FeatureCollection.i"
  "${SDK_BASE_DIR}/all/modules/geometry/VectorTileFeature.i"
  "${SDK_BASE_DIR}/all/modules/geometry/VectorTileFeatureCollection.i"
  # For the projection flags: Projection declares a position (bounds), and the GeoJSON writer is
  # the lightest class that declares a projection of its own.
  "${SDK_BASE_DIR}/all/modules/projections/Projection.i"
  # A concrete projection too, so writing an object property can be checked against a SUBCLASS of
  # what the property declares - the case the chain walk exists for.
  "${SDK_BASE_DIR}/all/modules/projections/EPSG3857.i"
  "${SDK_BASE_DIR}/all/modules/geometry/GeoJSONGeometryWriter.i"
  # For call/getData: TileData is the lightest class with a BinaryData property.
  "${SDK_BASE_DIR}/all/modules/datasources/components/TileData.i"
  "${SDK_BASE_DIR}/all/modules/core/BinaryData.i"
  # For routing: a request, a result and the service base need only a projection. The concrete
  # Valhalla services need sqlite and the routing library and stay out - see ../README.md.
  "${SDK_BASE_DIR}/all/modules/routing/RoutingService.i"
  "${SDK_BASE_DIR}/all/modules/routing/RoutingRequest.i"
  "${SDK_BASE_DIR}/all/modules/routing/RoutingResult.i"
  "${SDK_BASE_DIR}/all/modules/routing/RoutingInstruction.i"
  # Log is the SDK's only all-static class, and the only way to exercise a class with no instance.
  "${SDK_BASE_DIR}/all/modules/utils/Log.i"
)

# The routing modules are behind this, so the generator and the compile have to agree on it.
set(TEST_DEFINES "_MASSIF_ROUTING_SUPPORT")

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
  # GeometryMethods.cpp is the light half of the built-ins - it needs nothing but the geometry
  # classes, which is why it was split out of MethodImpls.cpp.
  "${SDK_BASE_DIR}/all/native/api/GeometryMethods.cpp"
  "${SDK_BASE_DIR}/all/native/api/RoutingMethods.cpp"
  "${SDK_BASE_DIR}/all/native/routing/RoutingService.cpp"
  "${SDK_BASE_DIR}/all/native/routing/RoutingRequest.cpp"
  "${SDK_BASE_DIR}/all/native/routing/RoutingResult.cpp"
  "${SDK_BASE_DIR}/all/native/routing/RoutingInstruction.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/VectorTileFeatureCollection.cpp"
  # MassifApiC.cpp is the flat ABI; Builtins.cpp is NOT here - it pulls in every source and layer,
  # so CAbiTest.cpp defines registerBuiltins() itself over a handful of classes.
  "${SDK_BASE_DIR}/all/native/api/MassifApiC.cpp"
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
         "${TEST_TABLE_DIR}/SpecConstructors.inc"
  COMMAND "${TEST_PYTHON}" "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
          "--modules" "${TEST_MODULE_LIST}"
          "--defines" "${TEST_DEFINES}"
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
  CAbiTest.cpp
  "${TEST_TABLE_DIR}/PropertyTable.inc"
  ${TEST_SDK_SOURCES})

set_source_files_properties("${TEST_TABLE_DIR}/PropertyTable.inc" PROPERTIES HEADER_FILE_ONLY TRUE)

target_compile_definitions(api_tests PRIVATE ${TEST_DEFINES})

target_include_directories(api_tests PRIVATE
  "${SDK_BASE_DIR}/all/native"
  "${TEST_TABLE_DIR}/.."
  "${SDK_BASE_DIR}/libs-external/cglib"
  "${SDK_BASE_DIR}/libs-external/stdext"
  "${SDK_BASE_DIR}/libs-external/picojson"
  # RoutingRequest uses boost::algorithm for its parameter paths - see ../../BUILDING.md for the
  # symlink this expects.
  "${SDK_BASE_DIR}/libs-external/boost"
  "${SDK_BASE_DIR}/libs-external/rapidjson/include"
  "${SDK_BASE_DIR}/libs-external/tinyformat")

add_test(NAME api COMMAND api_tests)
