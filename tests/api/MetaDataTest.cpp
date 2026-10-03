/*
 * Tests for the tile meta data map - the source-level bag that every TileData carries, and the
 * per-tile elevation decoder resolved from its "dem_encoding" entry.
 *
 * NOT covered here: PersistentCacheTileDataSource's stored copy of the map, which needs sqlite,
 * and anything that renders. See tests/README.md. The real archives' adoption of their "encoding"
 * is in ../archive.
 */

#include "core/MapTile.h"
#include "core/Variant.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"
#include "rastertiles/ElevationDecoder.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"

#include <map>
#include <memory>
#include <string>

using namespace massif;

#include "TestCheck.h"

namespace {

    /** The minimum concrete source: loadTile stamps the source's map, as every real one does. */
    struct StubTileDataSource : public TileDataSource {
        std::string containerEncoding;

        StubTileDataSource() : TileDataSource(0, 14) { }

        std::shared_ptr<TileData> loadTile(const MapTile&) override {
            auto tileData = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
            applyTileMetaData(tileData);
            return tileData;
        }

        std::string getContainerMetaData(const std::string& key) const override {
            return key == ElevationDecoder::ENCODING_KEY ? containerEncoding : std::string();
        }
    };

    /** An archive declaring its encoding the way MBTiles and PMTiles do, under "encoding". */
    struct ArchiveStub : public StubTileDataSource {
        std::string encoding;

        explicit ArchiveStub(const std::string& encoding) : encoding(encoding) {
            adoptContainerDemEncoding();
        }

        std::string getContainerMetaData(const std::string& key) const override {
            return key == "encoding" ? encoding : std::string();
        }
    };

    bool isTerrarium(const std::shared_ptr<ElevationDecoder>& decoder) {
        return std::dynamic_pointer_cast<TerrariumElevationDataDecoder>(decoder) != nullptr;
    }

    bool isMapBox(const std::shared_ptr<ElevationDecoder>& decoder) {
        return std::dynamic_pointer_cast<MapBoxElevationDataDecoder>(decoder) != nullptr;
    }

    Variant str(const std::string& value) {
        return Variant(value);
    }

}

void testTileMetaData() {
    auto source = std::make_shared<StubTileDataSource>();

    // Nothing set anywhere: an absent key is null, not an empty string.
    TEST_CHECK(source->getMetaDataElement("dem_encoding").getType() == VariantType::VARIANT_TYPE_NULL,
               "an unset key resolves to a null variant");
    TEST_CHECK(!source->containsMetaDataKey("dem_encoding"), "an unset key is not reported present");

    // The container declares it, the application does not: the fallback answers.
    source->containerEncoding = "terrarium";
    TEST_CHECK(source->getMetaDataElement("dem_encoding").getString() == "terrarium",
               "the container's own metadata answers when the source carries none");

    // An explicit entry wins over the container.
    source->setMetaDataElement("dem_encoding", str("mapbox"));
    TEST_CHECK(source->getMetaDataElement("dem_encoding").getString() == "mapbox",
               "an explicitly set entry wins over the container's");

    // A loaded tile carries the map.
    std::shared_ptr<TileData> tile1 = source->loadTile(MapTile(0, 0, 0, 0));
    TEST_CHECK(tile1->getMetaDataElement("dem_encoding").getString() == "mapbox",
               "a loaded tile carries the source's meta data");

    // The map is SHARED, not copied - that is what makes the stamp free. Writing on one tile must
    // therefore copy first, or every other tile of the source changes with it.
    std::shared_ptr<TileData> tile2 = source->loadTile(MapTile(0, 0, 0, 0));
    TEST_CHECK(tile1->getMetaData() == tile2->getMetaData(), "two tiles of one source share one map");
    tile2->setMetaDataElement("dem_encoding", str("terrarium"));
    TEST_CHECK(tile1->getMetaDataElement("dem_encoding").getString() == "mapbox",
               "writing on one tile does not change another's map");
    TEST_CHECK(source->getMetaDataElement("dem_encoding").getString() == "mapbox",
               "writing on a tile does not change the source's map");

    // setMetaData replaces wholesale, and an empty map clears back to the container fallback.
    std::map<std::string, Variant> replacement;
    replacement["other"] = str("x");
    source->setMetaData(replacement);
    TEST_CHECK(source->getMetaDataElement("dem_encoding").getString() == "terrarium",
               "setMetaData drops the old entries and falls back to the container");
    source->setMetaData(std::map<std::string, Variant>());
    TEST_CHECK(source->getMetaData().empty(), "setMetaData with an empty map clears the map");
}

void testElevationDecoderResolve() {
    auto source = std::make_shared<StubTileDataSource>();
    std::shared_ptr<ElevationDecoder> preferred = std::make_shared<TerrariumElevationDataDecoder>();

    // Nothing declared: the preferred decoder, then MapBox.
    TEST_CHECK(ElevationDecoder::Resolve(nullptr, nullptr, preferred) == preferred,
               "the preferred decoder is used when nothing declares one");
    TEST_CHECK(isMapBox(ElevationDecoder::Resolve(nullptr, nullptr, nullptr)),
               "MapBox is the default when nothing declares one");

    // The source declares it.
    source->setMetaDataElement("dem_encoding", str("terrarium"));
    TEST_CHECK(isTerrarium(ElevationDecoder::Resolve(nullptr, source, nullptr)),
               "the source's dem_encoding selects the decoder");

    // A source entry beats the caller's decoder, which is only a fallback.
    TEST_CHECK(isTerrarium(ElevationDecoder::Resolve(nullptr, source, std::make_shared<MapBoxElevationDataDecoder>())),
               "the source's dem_encoding wins over the preferred decoder");

    // The TILE beats the source - the whole point: two encodings behind one wrapper source.
    auto tile = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
    tile->setMetaDataElement("dem_encoding", str("mapbox"));
    TEST_CHECK(isMapBox(ElevationDecoder::Resolve(tile, source, nullptr)),
               "the tile's dem_encoding wins over the source's");

    // An unknown value is an error, not a silent pick: fall back rather than guess.
    auto badTile = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
    badTile->setMetaDataElement("dem_encoding", str("gopher"));
    TEST_CHECK(ElevationDecoder::Resolve(badTile, nullptr, preferred) == preferred,
               "an unknown dem_encoding falls back to the preferred decoder");

    // A non-string value must not be read as one either.
    auto numericTile = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
    numericTile->setMetaDataElement("dem_encoding", Variant(static_cast<long long>(3)));
    TEST_CHECK(ElevationDecoder::Resolve(numericTile, nullptr, preferred) == preferred,
               "a non-string dem_encoding falls back to the preferred decoder");
}

void testContainerDemEncoding() {
    auto terrarium = std::make_shared<ArchiveStub>("terrarium");
    TEST_CHECK(terrarium->getMetaDataElement("dem_encoding").getString() == "terrarium",
               "a container's terrarium encoding is adopted as dem_encoding");
    TEST_CHECK(isTerrarium(ElevationDecoder::Resolve(terrarium->loadTile(MapTile(0, 0, 0, 0)), nullptr, nullptr)),
               "the adopted dem_encoding is stamped on every tile, so a wrapper source still resolves it");
    TEST_CHECK(terrarium->getMetaDataElement("encoding").getString() == "terrarium",
               "the container's own encoding key still answers as before");

    std::map<std::string, Variant> unrelated;
    unrelated["other"] = str("x");
    terrarium->setMetaData(unrelated);
    TEST_CHECK(terrarium->getMetaDataElement("dem_encoding").getString() == "terrarium",
               "setMetaData without dem_encoding keeps the adopted one");
    TEST_CHECK(terrarium->getMetaDataElement("other").getString() == "x", "and still sets the rest");

    std::map<std::string, Variant> explicitMapBox;
    explicitMapBox["dem_encoding"] = str("mapbox");
    terrarium->setMetaData(explicitMapBox);
    TEST_CHECK(isMapBox(ElevationDecoder::Resolve(terrarium->loadTile(MapTile(0, 0, 0, 0)), nullptr, nullptr)),
               "an explicit dem_encoding in setMetaData wins over the container's");

    auto overridden = std::make_shared<ArchiveStub>("terrarium");
    overridden->setMetaDataElement("dem_encoding", str("mapbox"));
    TEST_CHECK(overridden->getMetaDataElement("dem_encoding").getString() == "mapbox",
               "an explicit setMetaDataElement wins over the container's");

    TEST_CHECK(std::make_shared<ArchiveStub>("mapbox")->getMetaDataElement("dem_encoding").getString() == "mapbox",
               "a container's mapbox encoding is adopted too");

    // Vector archives name their tile FORMAT under the same key.
    for (const char* vectorEncoding : { "mlt", "mvt", "pbf", "" }) {
        auto vector = std::make_shared<ArchiveStub>(vectorEncoding);
        TEST_CHECK(vector->getMetaData().empty() && !vector->containsMetaDataKey("dem_encoding"),
                   "a non-DEM container encoding adopts nothing");
    }
}
