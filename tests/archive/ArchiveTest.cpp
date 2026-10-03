/*
 * The archive sources' own metadata, over an MBTiles and a PMTiles written here: a DEM archive's
 * "encoding" ("terrarium" / "mapbox") is adopted as "dem_encoding", a vector archive's is not.
 *
 * NOT covered here: anything that decodes or renders a tile. The adoption rules themselves (what
 * setMetaData keeps, what wins) are in ../api/MetaDataTest.cpp. See ../README.md.
 */

#include "core/MapTile.h"
#include "core/Variant.h"
#include "datasources/MBTilesTileDataSource.h"
#include "datasources/PMTilesTileDataSource.h"
#include "datasources/components/TileData.h"
#include "rastertiles/ElevationDecoder.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include <sqlite3pp.h>

using namespace massif;

#include "TestCheck.h"

int failures = 0;

namespace {

    std::string tempPath(const std::string& name) {
        return (std::filesystem::temp_directory_path() / ("massif_archive_" + std::to_string(getpid()) + "_" + name)).string();
    }

    std::string writeMBTiles(const std::string& name, const std::string& encoding) {
        std::string path = tempPath(name);
        std::filesystem::remove(path);
        sqlite3pp::database db(path.c_str());
        db.execute("CREATE TABLE metadata (name TEXT, value TEXT)");
        db.execute("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)");
        db.execute("INSERT INTO metadata VALUES ('minzoom', '0'), ('maxzoom', '0')");
        if (!encoding.empty()) {
            sqlite3pp::command insert(db, "INSERT INTO metadata VALUES ('encoding', ?)");
            insert.bind(1, encoding, false, false);
            insert.execute();
        }
        db.execute("INSERT INTO tiles VALUES (0, 0, 0, x'00010203')");
        return path;
    }

    void writeUInt64(std::vector<std::uint8_t>& out, std::size_t offset, std::uint64_t value) {
        for (int i = 0; i < 8; i++) {
            out[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
        }
    }

    // One z0 tile, uncompressed directory and metadata (PMTiles v3).
    std::string writePMTiles(const std::string& name, const std::string& metadata, std::uint8_t tileType) {
        const std::string tile = "tile";
        const std::vector<std::uint8_t> directory = { 1, 0, 1, static_cast<std::uint8_t>(tile.size()), 1 };
        std::vector<std::uint8_t> header(127, 0);
        std::copy_n("PMTiles", 7, header.begin());
        header[7] = 3;
        std::uint64_t offset = header.size();
        writeUInt64(header, 8, offset);
        writeUInt64(header, 16, directory.size());
        offset += directory.size();
        writeUInt64(header, 24, offset);
        writeUInt64(header, 32, metadata.size());
        offset += metadata.size();
        writeUInt64(header, 40, offset);
        writeUInt64(header, 56, offset);
        writeUInt64(header, 64, tile.size());
        writeUInt64(header, 72, 1);
        writeUInt64(header, 80, 1);
        writeUInt64(header, 88, 1);
        header[96] = 1;
        header[97] = 1;
        header[98] = 1;
        header[99] = tileType;

        std::string path = tempPath(name);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
        file.write(reinterpret_cast<const char*>(directory.data()), directory.size());
        file << metadata << tile;
        return path;
    }

    bool resolvesTerrarium(const std::shared_ptr<TileData>& tile) {
        return std::dynamic_pointer_cast<TerrariumElevationDataDecoder>(ElevationDecoder::Resolve(tile, nullptr, nullptr)) != nullptr;
    }

    bool hasDemEncoding(const TileDataSource& source) {
        return source.containsMetaDataKey(ElevationDecoder::ENCODING_KEY);
    }

}

void testMBTilesEncoding() {
    std::string terrariumPath = writeMBTiles("terrarium.mbtiles", "terrarium");
    {
        MBTilesTileDataSource source(terrariumPath);
        TEST_CHECK(source.getMetaDataElement("dem_encoding").getString() == "terrarium",
                   "an MBTiles DEM's encoding row is adopted as dem_encoding");
        std::shared_ptr<TileData> tile = source.loadTile(MapTile(0, 0, 0, 0));
        TEST_CHECK(tile && resolvesTerrarium(tile), "an MBTiles DEM tile carries its encoding to the decoder");
    }
    {
        MBTilesTileDataSource source(0, 0, terrariumPath, MBTilesScheme::MBTILES_SCHEME_TMS);
        std::map<std::string, Variant> metaData;
        metaData["dem_encoding"] = Variant(std::string("mapbox"));
        source.setMetaData(metaData);
        TEST_CHECK(source.getMetaDataElement("dem_encoding").getString() == "mapbox",
                   "an explicit dem_encoding wins over the MBTiles encoding row");
    }
    std::filesystem::remove(terrariumPath);

    std::string vectorPath = writeMBTiles("vector.mbtiles", "mlt");
    {
        MBTilesTileDataSource source(vectorPath);
        TEST_CHECK(!hasDemEncoding(source), "an MBTiles vector encoding is not taken for a DEM one");
        TEST_CHECK(source.getMetaDataElement("encoding").getString() == "mlt",
                   "an MBTiles vector encoding still answers under its own key, for the tile format");
    }
    std::filesystem::remove(vectorPath);

    std::string plainPath = writeMBTiles("plain.mbtiles", "");
    {
        MBTilesTileDataSource source(0, 0, plainPath);
        TEST_CHECK(!hasDemEncoding(source) && source.getMetaData().empty(),
                   "an MBTiles with no encoding row carries no meta data");
    }
    std::filesystem::remove(plainPath);
}

void testPMTilesEncoding() {
    std::string terrariumPath = writePMTiles("terrarium.pmtiles", R"({"encoding":"terrarium","format":"webp","name":"dem"})", 4);
    {
        PMTilesTileDataSource source(terrariumPath);
        TEST_CHECK(source.getMetaDataElement("dem_encoding").getString() == "terrarium",
                   "a PMTiles DEM's metadata encoding is adopted as dem_encoding");
        std::shared_ptr<TileData> tile = source.loadTile(MapTile(0, 0, 0, 0));
        TEST_CHECK(tile && tile->getData() && resolvesTerrarium(tile), "a PMTiles DEM tile carries its encoding to the decoder");
    }
    {
        PMTilesTileDataSource source(0, 0, terrariumPath);
        source.setMetaDataElement("dem_encoding", Variant(std::string("mapbox")));
        TEST_CHECK(source.getMetaDataElement("dem_encoding").getString() == "mapbox",
                   "an explicit dem_encoding wins over the PMTiles metadata");
    }
    std::filesystem::remove(terrariumPath);

    std::string vectorPath = writePMTiles("vector.pmtiles", R"({"encoding":"mvt","vector_layers":[{"id":"water"}]})", 1);
    {
        PMTilesTileDataSource source(vectorPath);
        TEST_CHECK(!hasDemEncoding(source), "a PMTiles vector encoding is not taken for a DEM one");
        TEST_CHECK(source.getMetaDataElement("encoding").getString() == "mvt",
                   "a PMTiles vector encoding still answers under its own key, for the tile format");
    }
    std::filesystem::remove(vectorPath);
}

int main() {
    testMBTilesEncoding();
    testPMTilesEncoding();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
