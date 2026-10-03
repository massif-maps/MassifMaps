# The two archive sources, over small files each test writes itself. A binary of its own: MBTiles
# needs sqlite and PMTiles brotli, which the api tests' link stays without (see ../README.md).
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)

file(GLOB BROTLI_SOURCES
  "${SDK_BASE_DIR}/libs-external/brotli/brotli/c/common/*.c"
  "${SDK_BASE_DIR}/libs-external/brotli/brotli/c/dec/*.c")

add_executable(archive_tests
  ArchiveTest.cpp
  "${SDK_BASE_DIR}/all/native/datasources/MBTilesTileDataSource.cpp"
  "${SDK_BASE_DIR}/all/native/datasources/PMTilesTileDataSource.cpp"
  "${SDK_BASE_DIR}/all/native/datasources/components/PMTilesUtils.cpp"
  "${SDK_BASE_DIR}/all/native/datasources/TileDataSource.cpp"
  "${SDK_BASE_DIR}/all/native/datasources/components/TileData.cpp"
  "${SDK_BASE_DIR}/all/native/rastertiles/ElevationDecoder.cpp"
  "${SDK_BASE_DIR}/all/native/rastertiles/MapBoxElevationDataDecoder.cpp"
  "${SDK_BASE_DIR}/all/native/rastertiles/TerrariumElevationDataDecoder.cpp"
  "${SDK_BASE_DIR}/all/native/core/BinaryData.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapTile.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapPos.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapVec.cpp"
  "${SDK_BASE_DIR}/all/native/core/MapBounds.cpp"
  "${SDK_BASE_DIR}/all/native/core/Variant.cpp"
  "${SDK_BASE_DIR}/all/native/projections/Projection.cpp"
  "${SDK_BASE_DIR}/all/native/projections/EPSG3857.cpp"
  "${SDK_BASE_DIR}/all/native/utils/Const.cpp"
  "${SDK_BASE_DIR}/all/native/utils/Log.cpp"
  "${SDK_BASE_DIR}/all/native/utils/GeneralUtils.cpp"
  "${SDK_BASE_DIR}/libs-external/sqlite3pp/sqlite3pp.cpp"
  ${BROTLI_SOURCES})

target_compile_definitions(archive_tests PRIVATE _MASSIF_OFFLINE_SUPPORT)

target_include_directories(archive_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../api"
  "${SDK_BASE_DIR}/all/native"
  "${SDK_BASE_DIR}/libs-external/cglib"
  "${SDK_BASE_DIR}/libs-external/stdext"
  "${SDK_BASE_DIR}/libs-external/picojson"
  "${SDK_BASE_DIR}/libs-external/boost"
  "${SDK_BASE_DIR}/libs-external/rapidjson/include"
  "${SDK_BASE_DIR}/libs-external/tinyformat"
  "${SDK_BASE_DIR}/libs-external/sqlite3pp"
  "${SDK_BASE_DIR}/libs-external/brotli/brotli/c/include")

target_link_libraries(archive_tests PRIVATE SQLite::SQLite3 ZLIB::ZLIB)

add_test(NAME archive COMMAND archive_tests)
