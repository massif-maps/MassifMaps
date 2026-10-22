/*
 * The archive sources' own metadata, over an MBTiles and a PMTiles written here: a DEM archive's
 * "encoding" ("terrarium" / "mapbox") is adopted as "dem_encoding", a vector archive's is not.
 *
 * NOT covered here: anything that decodes or renders a tile. The adoption rules themselves (what
 * setMetaData keeps, what wins) are in ../api/MetaDataTest.cpp. See ../README.md.
 */

#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "core/Variant.h"
#include "datasources/MBTilesTileDataSource.h"
#include "datasources/PMTilesTileDataSource.h"
#include "datasources/PersistentCacheTileDataSource.h"
#include "datasources/components/PMTilesUtils.h"
#include "datasources/components/TileData.h"
#include "rastertiles/ElevationDecoder.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "utils/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>
#include <zlib.h>

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

    void putVarint(std::vector<std::uint8_t>& out, std::uint64_t value) {
        while (value >= 0x80) {
            out.push_back(static_cast<std::uint8_t>(value | 0x80));
            value >>= 7;
        }
        out.push_back(static_cast<std::uint8_t>(value));
    }

    std::vector<std::uint8_t> gzipBytes(const std::vector<std::uint8_t>& data) {
        z_stream stream;
        std::memset(&stream, 0, sizeof(stream));
        deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
        std::vector<std::uint8_t> out(deflateBound(&stream, static_cast<uLong>(data.size())));
        stream.next_in = const_cast<std::uint8_t*>(data.data());
        stream.avail_in = static_cast<uInt>(data.size());
        stream.next_out = out.data();
        stream.avail_out = static_cast<uInt>(out.size());
        deflate(&stream, Z_FINISH);
        out.resize(stream.total_out);
        deflateEnd(&stream);
        return out;
    }

    // Offsets are written absolute (+1), never as "contiguous with the previous entry".
    std::vector<std::uint8_t> encodeDirectory(const std::vector<pmtiles::DirectoryEntry>& entries) {
        std::vector<std::uint8_t> out;
        putVarint(out, entries.size());
        std::uint64_t last = 0;
        for (const auto& entry : entries) {
            putVarint(out, entry.tileId - last);
            last = entry.tileId;
        }
        for (const auto& entry : entries) {
            putVarint(out, entry.runLength);
        }
        for (const auto& entry : entries) {
            putVarint(out, entry.length);
        }
        for (const auto& entry : entries) {
            putVarint(out, entry.offset + 1);
        }
        return out;
    }

    constexpr int MULTI_LEAF_MAX_ZOOM = 5;
    constexpr std::uint64_t MULTI_LEAF_RUN_START = 1000;
    constexpr std::uint32_t MULTI_LEAF_RUN_LENGTH = 10;

    std::vector<std::uint8_t> multiLeafPayload(std::uint64_t tileId) {
        std::string text;
        for (int i = 0; i < 40; i++) {
            text += "tile-" + std::to_string(tileId) + ";";
        }
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    // Tiles z0..z5 spread over four leaf directories under a root of leaf pointers, directories and
    // tiles gzipped. One run of ten tiles shares a blob; (3,3,5) is absent.
    std::string writeMultiLeafPMTiles(const std::string& name, const pmtiles::DirectoryEntry*& missing) {
        static pmtiles::DirectoryEntry missingEntry;
        missingEntry.tileId = pmtiles::zxyToTileId(5, 3, 3);
        missing = &missingEntry;

        std::vector<std::uint8_t> tileData;
        std::vector<pmtiles::DirectoryEntry> tiles;
        std::uint64_t lastTileId = 0;
        std::uint64_t tileCount = 0;
        for (std::uint64_t tileId = 0; tileId < 1365; tileId++) {
            if (tileId == missingEntry.tileId) {
                continue;
            }
            if (tileId > MULTI_LEAF_RUN_START && tileId < MULTI_LEAF_RUN_START + MULTI_LEAF_RUN_LENGTH) {
                continue;
            }
            std::vector<std::uint8_t> blob = gzipBytes(multiLeafPayload(tileId));
            pmtiles::DirectoryEntry entry;
            entry.tileId = tileId;
            entry.offset = tileData.size();
            entry.length = static_cast<std::uint32_t>(blob.size());
            entry.runLength = tileId == MULTI_LEAF_RUN_START ? MULTI_LEAF_RUN_LENGTH : 1;
            tileData.insert(tileData.end(), blob.begin(), blob.end());
            tiles.push_back(entry);
            lastTileId = tileId;
            tileCount++;
        }
        (void)lastTileId;

        std::vector<std::uint8_t> leafData;
        std::vector<pmtiles::DirectoryEntry> root;
        const std::size_t perLeaf = 400;
        for (std::size_t begin = 0; begin < tiles.size(); begin += perLeaf) {
            std::vector<pmtiles::DirectoryEntry> chunk(tiles.begin() + begin, tiles.begin() + std::min(tiles.size(), begin + perLeaf));
            std::vector<std::uint8_t> leaf = gzipBytes(encodeDirectory(chunk));
            pmtiles::DirectoryEntry pointer;
            pointer.tileId = chunk.front().tileId;
            pointer.offset = leafData.size();
            pointer.length = static_cast<std::uint32_t>(leaf.size());
            pointer.runLength = 0;
            leafData.insert(leafData.end(), leaf.begin(), leaf.end());
            root.push_back(pointer);
        }
        std::vector<std::uint8_t> rootData = gzipBytes(encodeDirectory(root));
        const std::string metadataJson = "{}";
        const std::vector<std::uint8_t> metadata = gzipBytes(std::vector<std::uint8_t>(metadataJson.begin(), metadataJson.end()));

        std::vector<std::uint8_t> header(127, 0);
        std::copy_n("PMTiles", 7, header.begin());
        header[7] = 3;
        std::uint64_t offset = header.size();
        writeUInt64(header, 8, offset);
        writeUInt64(header, 16, rootData.size());
        offset += rootData.size();
        writeUInt64(header, 24, offset);
        writeUInt64(header, 32, metadata.size());
        offset += metadata.size();
        writeUInt64(header, 40, offset);
        writeUInt64(header, 48, leafData.size());
        offset += leafData.size();
        writeUInt64(header, 56, offset);
        writeUInt64(header, 64, tileData.size());
        writeUInt64(header, 72, tileCount);
        writeUInt64(header, 80, tiles.size());
        writeUInt64(header, 88, tiles.size());
        header[96] = 1;
        header[97] = 2;
        header[98] = 2;
        header[99] = 1;
        header[101] = MULTI_LEAF_MAX_ZOOM;

        std::string path = tempPath(name);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
        file.write(reinterpret_cast<const char*>(rootData.data()), rootData.size());
        file.write(reinterpret_cast<const char*>(metadata.data()), metadata.size());
        file.write(reinterpret_cast<const char*>(leafData.data()), leafData.size());
        file.write(reinterpret_cast<const char*>(tileData.data()), tileData.size());
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

void testPMTilesDirectorySearch() {
    using pmtiles::DirectoryEntry;
    std::vector<DirectoryEntry> directory(3);
    directory[0] = { 10, 0, 5, 3 };   // tiles 10..12
    directory[1] = { 20, 0, 0, 0 };   // leaf pointer from 20
    directory[2] = { 40, 9, 5, 1 };   // tile 40

    TEST_CHECK(pmtiles::findDirectoryEntry(directory, 9) == nullptr, "a PMTiles id before the first entry matches nothing");
    TEST_CHECK(pmtiles::findDirectoryEntry(directory, 12) == &directory[0], "the last tile of a run is found");
    TEST_CHECK(pmtiles::findDirectoryEntry(directory, 13) == nullptr, "the id after a run, before the next entry, matches nothing");
    TEST_CHECK(pmtiles::findDirectoryEntry(directory, 35) == &directory[1], "an id past a leaf pointer is answered by that pointer");
    TEST_CHECK(pmtiles::findDirectoryEntry(directory, 41) == nullptr, "the id after a single tile past the end matches nothing");
    DirectoryEntry found;
    TEST_CHECK(!pmtiles::findTileEntry(directory, 25, found) && pmtiles::findTileEntry(directory, 11, found) && found.tileId == 10,
               "findTileEntry never returns a leaf pointer");
}

void testPMTilesMultiLeaf() {
    const pmtiles::DirectoryEntry* missing = nullptr;
    std::string path = writeMultiLeafPMTiles("multileaf.pmtiles", missing);
    {
        PMTilesTileDataSource source(path);
        int wrong = 0;
        int checked = 0;
        for (int zoom = 0; zoom <= MULTI_LEAF_MAX_ZOOM; zoom++) {
            for (int x = 0; x < (1 << zoom); x++) {
                for (int y = 0; y < (1 << zoom); y++) {
                    std::uint64_t tileId = pmtiles::zxyToTileId(zoom, x, y);
                    std::shared_ptr<TileData> tile = source.loadTile(MapTile(x, y, zoom, 0));
                    if (tileId == missing->tileId) {
                        continue;
                    }
                    // a run answers every id in it with its first tile's blob
                    std::uint64_t blobId = tileId > MULTI_LEAF_RUN_START && tileId < MULTI_LEAF_RUN_START + MULTI_LEAF_RUN_LENGTH ? MULTI_LEAF_RUN_START : tileId;
                    checked++;
                    if (!tile || !tile->getData() || *tile->getData()->getDataPtr() != multiLeafPayload(blobId)) {
                        wrong++;
                    }
                }
            }
        }
        TEST_CHECK(checked == 1365 - 1 && wrong == 0, "every tile of a PMTiles over several gzipped leaf directories loads with its own bytes");

        std::shared_ptr<TileData> absent = source.loadTile(MapTile(3, 3, 5, 0));
        TEST_CHECK(absent && absent->isReplaceWithParent(), "a tile missing from a PMTiles inside its zoom range asks for its parent");

        std::atomic<int> threadWrong(0);
        std::vector<std::thread> threads;
        for (int index = 0; index < 8; index++) {
            threads.emplace_back([&source, &threadWrong, index]() {
                for (int round = 0; round < 3; round++) {
                    for (int zoom = MULTI_LEAF_MAX_ZOOM; zoom >= 0; zoom--) {
                        for (int x = 0; x < (1 << zoom); x++) {
                            int rotated = (x + index * 7) % (1 << zoom);
                            for (int y = 0; y < (1 << zoom); y++) {
                                std::uint64_t tileId = pmtiles::zxyToTileId(zoom, rotated, y);
                                if (tileId == pmtiles::zxyToTileId(5, 3, 3) || (tileId > MULTI_LEAF_RUN_START && tileId < MULTI_LEAF_RUN_START + MULTI_LEAF_RUN_LENGTH)) {
                                    continue;
                                }
                                std::shared_ptr<TileData> tile = source.loadTile(MapTile(rotated, y, zoom, 0));
                                if (!tile || !tile->getData() || *tile->getData()->getDataPtr() != multiLeafPayload(tileId)) {
                                    threadWrong++;
                                }
                            }
                        }
                    }
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        TEST_CHECK(threadWrong == 0, "concurrent loads from one PMTiles source return each tile's own bytes");
    }
    std::filesystem::remove(path);
}

// purgeExpired() drops a row past its expiration: only its own tile ever asked for it again, and
// a rolled-off animation frame is never asked for. Opening the cache alone drops nothing.
void testPersistentCachePurge() {
    std::string cachePath = tempPath("persistent.db");
    std::filesystem::remove(cachePath);
    long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    {
        sqlite3pp::database db(cachePath.c_str());
        db.execute("CREATE TABLE persistent_cache(tileId INTEGER NOT NULL PRIMARY KEY, compressed BLOB, time INTEGER, expirationTime INTEGER, metaData TEXT)");
        db.execute(("INSERT INTO persistent_cache VALUES (1, x'00', 1, " + std::to_string(now - 60000) + ", '')").c_str());
        db.execute(("INSERT INTO persistent_cache VALUES (2, x'00', 1, " + std::to_string(now + 3600000) + ", '')").c_str());
        db.execute("INSERT INTO persistent_cache VALUES (3, x'00', 1, 0, '')");
    }
    auto rows = [&cachePath]() {
        std::set<long long> ids;
        sqlite3pp::database db(cachePath.c_str());
        sqlite3pp::query query(db, "SELECT tileId FROM persistent_cache");
        for (auto it = query.begin(); it != query.end(); ++it) {
            ids.insert((*it).get<long long>(0));
        }
        return ids;
    };
    auto inner = std::make_shared<MBTilesTileDataSource>(writeMBTiles("inner.mbtiles", ""));
    {
        PersistentCacheTileDataSource cache(inner, cachePath);
    }
    TEST_CHECK(rows() == std::set<long long>({ 1, 2, 3 }), "opening the cache drops nothing, whatever its size");
    {
        PersistentCacheTileDataSource cache(inner, cachePath);
        cache.purgeExpired();
    }
    TEST_CHECK(rows() == std::set<long long>({ 2, 3 }), "purging drops the expired row, keeps the live and the never-expiring ones");
    std::filesystem::remove(cachePath);
}

int main() {
    Log::SetShowInfo(false); // one line per loadTile
    testMBTilesEncoding();
    testPMTilesEncoding();
    testPMTilesDirectorySearch();
    testPMTilesMultiLeaf();
    testPersistentCachePurge();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
