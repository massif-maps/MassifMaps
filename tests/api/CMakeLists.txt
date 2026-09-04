# The property table takes the address of every accessor thunk, so a table generated over the whole
# SDK would need the whole SDK to link. These tests generate a REDUCED table from an explicit list
# of modules instead, which keeps the link to a handful of files - and exercises the generator.
set(TEST_MODULES
  "${SDK_BASE_DIR}/all/modules/components/FogOptions.i"
  # The sky carries the only WRITABLE enum in the reduced table (SkyType), which is what checks
  # that a constant NAME is accepted and an unknown one is rejected rather than read as 0.
  "${SDK_BASE_DIR}/all/modules/components/SkyOptions.i"
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
  # The class a custom source registers AS - getObject checks it against the table, so without
  # this row mm_source_create_custom registers a source no layer spec could resolve.
  "${SDK_BASE_DIR}/all/modules/datasources/TileDataSource.i"
  "${SDK_BASE_DIR}/all/modules/core/BinaryData.i"
  # For routing: a request, a result and the service base need only a projection. The concrete
  # Valhalla services need sqlite and the routing library and stay out - see ../README.md.
  "${SDK_BASE_DIR}/all/modules/routing/RoutingService.i"
  "${SDK_BASE_DIR}/all/modules/routing/RoutingRequest.i"
  "${SDK_BASE_DIR}/all/modules/routing/RoutingResult.i"
  "${SDK_BASE_DIR}/all/modules/routing/RoutingInstruction.i"
  # A writable STRUCT property on a light class: Layer.visibleZoomRange is the one that matters in
  # practice, but a layer needs the renderer. This one is 55 lines and has a default constructor.
  "${SDK_BASE_DIR}/all/modules/geometry/VectorTileFeatureBuilder.i"
  # Log is the SDK's only all-static class, and the only way to exercise a class with no instance.
  "${SDK_BASE_DIR}/all/modules/utils/Log.i"
  # The only light class with a WRITABLE position - the set side of the WGS84 default (#159).
  "${SDK_BASE_DIR}/all/modules/geocoding/GeocodingRequest.i"
  # An ENUM property on a class with no dependencies at all - the map events' reason, and the only
  # enum in the reduced table.
  "${SDK_BASE_DIR}/all/modules/ui/MapMoveInfo.i"
  # Aliases the reduced table can carry - the production ones are on classes the host cannot link.
  "${CMAKE_CURRENT_SOURCE_DIR}/modules/TestFacade.i"
  # For adopt: an abstract base an app SUBCLASSES, which is what a spec's `assets` id resolves to.
  # The base only - DirAssetPackage's !spec would pull FileUtils, and with it the platform
  # AssetUtils, which does not exist on the host.
  "${SDK_BASE_DIR}/all/modules/utils/AssetPackage.i"
)

# The routing modules are behind this, so the generator and the compile have to agree on it.
set(TEST_DEFINES "_MASSIF_ROUTING_SUPPORT" "_MASSIF_GEOCODING_SUPPORT")

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
  "${SDK_BASE_DIR}/all/native/routing/RouteMatchingRequest.cpp"
  "${SDK_BASE_DIR}/all/native/routing/RoutingResult.cpp"
  "${SDK_BASE_DIR}/all/native/routing/RoutingInstruction.cpp"
  "${SDK_BASE_DIR}/all/native/geocoding/GeocodingRequest.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/VectorTileFeatureCollection.cpp"
  # MassifApiC.cpp is the flat ABI; Builtins.cpp is NOT here - it pulls in every source and layer,
  # so CAbiTest.cpp defines registerBuiltins() itself over a handful of classes.
  "${SDK_BASE_DIR}/all/native/api/MassifApiC.cpp"
  # The subscription half of MassifApi. The rest of MassifApi.cpp needs Options, Layers and every
  # source constructor, which is why these functions were split into a TU of their own.
  "${SDK_BASE_DIR}/all/native/api/MassifApiEvents.cpp"
  "${SDK_BASE_DIR}/all/native/core/BinaryData.cpp"
  "${SDK_BASE_DIR}/all/native/datasources/components/TileData.cpp"
  # What mm_source_create_custom builds. TileDataSource itself is already in the list below.
  "${SDK_BASE_DIR}/all/native/api/CallbackTileDataSource.cpp"
  "${SDK_BASE_DIR}/all/native/projections/Projection.cpp"
  "${SDK_BASE_DIR}/all/native/projections/EPSG3857.cpp"
  "${SDK_BASE_DIR}/all/native/projections/EPSG4326.cpp"
  # Spec.cpp holds create() and the registry; the built-in factories live in SpecFactories.cpp,
  # which is deliberately NOT here - linking it would need every source and layer constructor.
  "${SDK_BASE_DIR}/all/native/api/Spec.cpp"
  # The generated constructor builders. Light for THIS table: it only declares the geometry classes,
  # so the .inc only pulls their headers in - which is the whole point of the split.
  "${SDK_BASE_DIR}/all/native/api/SpecBuilders.cpp"
  "${SDK_BASE_DIR}/all/native/core/Variant.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapPos.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapVec.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapRange.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapBounds.cpp"
  "${SDK_BASE_DIR}/all/native/core/ScreenPos.cpp"
  # StructCodec carries a screen rectangle now - fitBounds takes one (see CameraMethods.cpp).
  "${SDK_BASE_DIR}/all/native/core/ScreenBounds.cpp"
  # StructCodec carries a day-cycle light curve now, and a stop is a value class.
  "${SDK_BASE_DIR}/all/native/components/LightStop.cpp"
  "${SDK_BASE_DIR}/all/native/components/FogOptions.cpp"
  "${SDK_BASE_DIR}/all/native/components/SkyOptions.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/Feature.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/VectorTileFeature.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/VectorTileFeatureBuilder.cpp"
  "${SDK_BASE_DIR}/all/native/geometry/FeatureBuilder.cpp"
  "${SDK_BASE_DIR}/all/native/ui/VectorTileClickInfo.cpp"
  "${SDK_BASE_DIR}/all/native/ui/MapMoveInfo.cpp"
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
  "${SDK_BASE_DIR}/all/native/geometry/Geometry.cpp"
  "${SDK_BASE_DIR}/all/native/graphics/Color.cpp"
  # BundleAssetsTest.cpp defines the platform AssetUtils these two declare - see host/utils.
  "${SDK_BASE_DIR}/all/native/utils/BundleAssetPackage.cpp"
  "${SDK_BASE_DIR}/all/native/utils/FileUtils.cpp"
  "${SDK_BASE_DIR}/all/native/utils/Log.cpp"
  "${SDK_BASE_DIR}/all/native/utils/GeomUtils.cpp"
  "${SDK_BASE_DIR}/all/native/utils/GeneralUtils.cpp"
  "${SDK_BASE_DIR}/all/native/utils/Const.cpp"
  # The tile meta data bag and the decoder it selects. TileDataSource pulls in nothing but the
  # projection and Variant, both already here; the two decoders are constants and a vtable.
  "${SDK_BASE_DIR}/all/native/datasources/TileDataSource.cpp"
  "${SDK_BASE_DIR}/all/native/rastertiles/ElevationDecoder.cpp"
  "${SDK_BASE_DIR}/all/native/rastertiles/MapBoxElevationDataDecoder.cpp"
  "${SDK_BASE_DIR}/all/native/rastertiles/TerrariumElevationDataDecoder.cpp"
)

set(TEST_TABLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/api")
list(JOIN TEST_MODULES "," TEST_MODULE_LIST)

find_program(TEST_PYTHON NAMES python3 python REQUIRED)

add_custom_command(
  OUTPUT "${TEST_TABLE_DIR}/PropertyTable.inc" "${TEST_TABLE_DIR}/PropertyAccessors.inc"
         "${TEST_TABLE_DIR}/SpecConstructors.inc" "${TEST_TABLE_DIR}/MethodDecls.inc"
  COMMAND "${TEST_PYTHON}" "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
          "--modules" "${TEST_MODULE_LIST}"
          "--defines" "${TEST_DEFINES}"
          "--cppdir" "${SDK_BASE_DIR}/all/native"
          "--outdir" "${TEST_TABLE_DIR}"
  DEPENDS ${TEST_MODULES} "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
  WORKING_DIRECTORY "${SDK_BASE_DIR}/scripts"
  COMMENT "Generating the reduced API property table for tests"
  VERBATIM)

# A SECOND, schema-only run over the archive sources. Their classes cannot be linked here - PMTiles
# needs zlib, brotli and zstd - but the schema is JSON, so SpecSchemaTest reads it without any of
# that. Separate outdir: these .inc files are deliberately not compiled.
set(TEST_SCHEMA_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/schema")
set(TEST_SCHEMA_MODULES
  "${SDK_BASE_DIR}/all/modules/datasources/PMTilesTileDataSource.i"
  "${SDK_BASE_DIR}/all/modules/datasources/MBTilesTileDataSource.i"
  "${SDK_BASE_DIR}/all/modules/datasources/TileDataSource.i"
)
list(JOIN TEST_SCHEMA_MODULES "," TEST_SCHEMA_MODULE_LIST)

add_custom_command(
  OUTPUT "${TEST_SCHEMA_DIR}/massif-api.json"
  COMMAND "${TEST_PYTHON}" "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
          "--modules" "${TEST_SCHEMA_MODULE_LIST}"
          "--defines" "_MASSIF_OFFLINE_SUPPORT"
          "--cppdir" "${SDK_BASE_DIR}/all/native"
          "--outdir" "${TEST_SCHEMA_DIR}"
          "--schema" "${TEST_SCHEMA_DIR}/massif-api.json"
  DEPENDS ${TEST_SCHEMA_MODULES} "${SDK_BASE_DIR}/scripts/gen-api-tables.py"
  WORKING_DIRECTORY "${SDK_BASE_DIR}/scripts"
  COMMENT "Generating the source spec schema for tests"
  VERBATIM)

add_executable(api_tests
  ApiTest.cpp
  EventTest.cpp
  StructCodecTest.cpp
  ProjectionTest.cpp
  MethodTest.cpp
  AdoptTest.cpp
  CustomSourceTest.cpp
  BundleAssetsTest.cpp
  CAbiTest.cpp
  MetaDataTest.cpp
  BagTest.cpp
  FogSkyTest.cpp
  SpecKeyTest.cpp
  # Reads the two schemas below, links nothing of its own.
  SpecSchemaTest.cpp
  # Header-only: AutoFlatten.h has no dependencies at all, so this costs the link nothing.
  AutoFlattenTest.cpp
  # Same: CameraClearance.h is mapbox's minimum-height-over-terrain rule and nothing else.
  CameraClearanceTest.cpp
  # Same: DrapeStackCuts.h is the ordering rule and nothing else.
  DrapeStackCutsTest.cpp
  # Same: FlattenSwitch.h is the 2D/3D switch's phases and nothing else.
  FlattenSwitchTest.cpp
  # Same: ElevationNodeField.h is the mesh-node box filter over a texel callback.
  ElevationNodeFieldTest.cpp
  # Same: DayCycleLight.h is the hour-to-light curve and mapbox's ground radiance, nothing else.
  DayCycleLightTest.cpp
  "${TEST_TABLE_DIR}/PropertyTable.inc"
  "${TEST_TABLE_DIR}/MethodDecls.inc"
  "${TEST_SCHEMA_DIR}/massif-api.json"
  ${TEST_SDK_SOURCES})

set_source_files_properties("${TEST_TABLE_DIR}/PropertyTable.inc" PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties("${TEST_SCHEMA_DIR}/massif-api.json" PROPERTIES HEADER_FILE_ONLY TRUE)

target_compile_definitions(api_tests PRIVATE ${TEST_DEFINES}
  "TEST_SCHEMA_PATH=\"${TEST_SCHEMA_DIR}/massif-api.json\""
  "SDK_SCHEMA_PATH=\"${SDK_BASE_DIR}/docs/api/massif-api.json\"")

target_include_directories(api_tests PRIVATE
  "${SDK_BASE_DIR}/all/native"
  # A host stand-in for the per-platform utils/AssetUtils.h, which all/native does not carry.
  "${CMAKE_CURRENT_SOURCE_DIR}/host"
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
