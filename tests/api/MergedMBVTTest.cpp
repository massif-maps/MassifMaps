/*
 * Tests for MergedMBVTTileDataSource past a source's max zoom: the source's last tile is cut into the
 * requested one, up to the source's max overzoom level when one is set.
 *
 * NOT covered here: the archive sources it merges in the app (MBTiles, PMTiles) and the layer's own
 * overzoom of a "replace with parent" answer, which needs the renderer. See tests/README.md.
 */

#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "datasources/MergedMBVTTileDataSource.h"
#include "datasources/components/TileData.h"

#include <mapnikvt/MBVTSubtile.h>

#include <algorithm>
#include <memory>
#include <string>

using namespace massif;

#include "TestCheck.h"

namespace {

    std::vector<unsigned char> layerTile(const std::string& name) {
        namespace detail = massif::mvt::subtile_detail;
        std::string layer;
        detail::writeBytes(layer, 1, name);
        detail::writeVarint(layer, 5 << 3);
        detail::writeVarint(layer, 4096);
        std::string tile;
        detail::writeBytes(tile, 3, layer);
        return std::vector<unsigned char>(tile.begin(), tile.end());
    }

    bool hasLayer(const std::shared_ptr<TileData>& tileData, const std::string& name) {
        if (!tileData || !tileData->getData()) {
            return false;
        }
        const std::vector<unsigned char>& data = *tileData->getData()->getDataPtr();
        return std::search(data.begin(), data.end(), name.begin(), name.end()) != data.end();
    }

    /** One layer at every zoom up to maxZoom; past an optional data zoom it answers "replace with parent". */
    struct LayerSource : public TileDataSource {
        std::string layerName;
        int dataMaxZoom;
        int loads = 0;

        LayerSource(const std::string& name, int maxZoom, int dataMaxZoom) :
            TileDataSource(0, maxZoom), layerName(name), dataMaxZoom(dataMaxZoom) { }

        std::shared_ptr<TileData> loadTile(const MapTile& tile) override {
            loads++;
            if (tile.getZoom() > dataMaxZoom) {
                auto tileData = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
                tileData->setReplaceWithParent(true);
                return tileData;
            }
            return std::make_shared<TileData>(std::make_shared<BinaryData>(layerTile(layerName)));
        }
    };

}

void testMergedOverzoom() {
    // The app's case: a z0-6 bathymetry archive over a chain whose z7 world tile is overzoomed by
    // the layer, the chain answering "replace with parent" outside its regions.
    auto chain = std::make_shared<LayerSource>("landcover", 14, 7);
    auto bathymap = std::make_shared<LayerSource>("depth", 6, 6);
    auto merged = std::make_shared<MergedMBVTTileDataSource>(chain, bathymap);

    TEST_CHECK(merged->getMaxZoom() == 14, "the merged max zoom is still the deeper source's");

    // Unset: the bathymap is cut at any zoom, so it hides the chain's "replace with parent".
    std::shared_ptr<TileData> unlimited = merged->loadTile(MapTile(512, 512, 10, 0));
    TEST_CHECK(unlimited && !unlimited->isReplaceWithParent() && hasLayer(unlimited, "depth"),
               "without a max overzoom level a source is cut into its last tile at any zoom");

    bathymap->setMaxOverzoomLevel(1);
    TEST_CHECK(merged->getMaxZoom() == 14, "a member's overzoom level does not change the merged max zoom");

    int loadsBefore = bathymap->loads;
    std::shared_ptr<TileData> past = merged->loadTile(MapTile(512, 512, 10, 0));
    TEST_CHECK(past && past->isReplaceWithParent(),
               "past its max overzoom level a source drops out, and the other's replace-with-parent goes through");
    TEST_CHECK(bathymap->loads == loadsBefore, "past its max overzoom level a source is not read at all");

    std::shared_ptr<TileData> firstPast = merged->loadTile(MapTile(64, 64, 8, 0));
    TEST_CHECK(firstPast && firstPast->isReplaceWithParent(), "max zoom + overzoom + 1 is already past it");

    std::shared_ptr<TileData> atLimit = merged->loadTile(MapTile(64, 64, 7, 0));
    TEST_CHECK(atLimit && !atLimit->isReplaceWithParent() && hasLayer(atLimit, "landcover") && hasLayer(atLimit, "depth"),
               "at max zoom + overzoom the source is still cut in, merged with the other");

    // Zero is a limit too: the source stops at its own max zoom.
    bathymap->setMaxOverzoomLevel(0);
    std::shared_ptr<TileData> noOverzoom = merged->loadTile(MapTile(64, 64, 7, 0));
    TEST_CHECK(noOverzoom && hasLayer(noOverzoom, "landcover") && !hasLayer(noOverzoom, "depth"),
               "an overzoom level of 0 stops the source at its own max zoom");
    std::shared_ptr<TileData> atMaxZoom = merged->loadTile(MapTile(32, 32, 6, 0));
    TEST_CHECK(atMaxZoom && hasLayer(atMaxZoom, "depth"), "a source within its max zoom is read as before");

    // Back to unset restores the old behaviour.
    bathymap->setMaxOverzoomLevel(-1);
    std::shared_ptr<TileData> reset = merged->loadTile(MapTile(512, 512, 10, 0));
    TEST_CHECK(reset && hasLayer(reset, "depth"), "unsetting the max overzoom level cuts at any zoom again");
}
