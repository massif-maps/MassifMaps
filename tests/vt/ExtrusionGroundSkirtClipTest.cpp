/*
 * The contact shadow's tile clip (TileLayerBuilder::appendGroundSkirt): a footprint lays its skirt
 * only in the tiles it reaches.
 *
 * Under overzoom every target tile derived from one source tile is handed ALL of that source's
 * features, so the clip is what keeps a z18 tile from carrying the skirts of a whole z14 tile. Its
 * bounding box was default-constructed - uninitialised floats - so the test kept whatever the garbage
 * made it keep. Measured on the web build, Paris z17.2 tilt 45 (the ground radius starts at z18):
 * every z18 tile decoded to 55 MB of skirts, the 512 MB visible cache overflowed, and the view's own
 * tiles were refetched forever.
 *
 * NOT covered here: the skirt's shading, and the walls' own per-edge clip.
 */

#include "TileLayerBuilder.h"

#include "TestCheck.h"

#include <vector>

using namespace massif::vt;

namespace {
    const TileId PARIS_Z18(18, 132775, 90182);

    // How many skirt indices one extruded square lays on this tile.
    std::size_t skirtIndices(float x0, float y0, float side) {
        auto transformer = std::make_shared<DefaultTileTransformer>(1.0f);
        TileLayerBuilder builder("test", 0, PARIS_Z18, transformer, 256.0f, 1.0f);
        builder.setPolygon3DGroundRadius(8.0f);

        Polygon3DStyle style(ColorFunction(Color(1, 1, 1, 1)), std::optional<Transform>());
        TileLayerBuilder::Polygon3DProcessor processor = builder.createPolygon3DProcessor(style);
        TileLayerBuilder::VerticesList verticesList;
        verticesList.push_back({ { x0, y0 }, { x0 + side, y0 }, { x0 + side, y0 + side }, { x0, y0 + side } });
        processor(1, verticesList, 0.0f, 20.0f);

        std::size_t indices = 0;
        std::shared_ptr<TileLayer> layer = builder.buildTileLayer();
        for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
            if (geometry->getType() == TileGeometry::Type::POLYGON3DGROUND) {
                indices += geometry->getIndices().size();
            }
        }
        return indices;
    }
}

void testExtrusionGroundSkirtClip() {
    TEST_CHECK(skirtIndices(0.4f, 0.4f, 0.1f) > 0, "a footprint inside the tile lays its contact shadow");
    TEST_CHECK(skirtIndices(0.95f, 0.4f, 0.1f) > 0, "a footprint straddling the tile edge lays it too");
    // Down and right of the tile: a box grown from (0, 0) instead of from the footprint reaches back
    // over the whole tile, which is the case the uninitialised box got wrong.
    TEST_CHECK(skirtIndices(3.0f, 3.0f, 0.1f) == 0, "a footprint two tiles away lays nothing here");
    TEST_CHECK(skirtIndices(-3.0f, -3.0f, 0.1f) == 0, "nor one two tiles away on the other side");
    TEST_CHECK(skirtIndices(0.4f, 5.0f, 0.1f) == 0, "nor one level with the tile but far below it");
}
