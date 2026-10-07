/*
 * A FULL 2D/3D switch changes a layer's tile transformer, and the GL renderer now takes the new one
 * in place (GLTileRenderer::setTransformer) instead of being rebuilt: a rebuilt renderer faded every
 * tile in from nothing. Checked here: the surface builder it hands the transformer to drops what it
 * built with the old one. GLTileRenderer itself needs GL, so the blank-frame fix is a browser check.
 */

#include "vt/TileSurface.h"
#include "vt/TileSurfaceBuilder.h"
#include "vt/TileTransformer.h"

#include <memory>
#include <set>
#include <vector>

using namespace massif::vt;

#include "TestCheck.h"

void testSurfaceTransformerSwap() {
    const float worldSize = static_cast<float>(1 << 20);
    auto planar = std::make_shared<DefaultTileTransformer>(worldSize);
    auto sphere = std::make_shared<SphericalTileTransformer>(worldSize / 3.1415926535897932f);
    TileId tileId(6, 20, 20);

    // Skirts on: a spherical surface carries the skirt attribute and a planar one does not, so the
    // layout tells which transformer built a surface.
    TileSurfaceBuilder builder(planar);
    builder.setVisibleTiles(std::set<TileId>{ tileId });
    builder.setTerrainSkirts(true);
    std::vector<std::shared_ptr<TileSurface>> before = builder.buildTileSurface(tileId);
    TEST_CHECK(!before.empty() && before.front()->getVertexGeometryLayoutParameters().skirtOffset < 0, "the planar transformer builds a planar surface");
    TEST_CHECK(builder.isTileSurfaceCached(tileId), "and caches it");

    builder.setTransformer(sphere);
    TEST_CHECK(!builder.isTileSurfaceCached(tileId), "a new transformer drops the surfaces the old one built");
    std::vector<std::shared_ptr<TileSurface>> after = builder.buildTileSurface(tileId);
    TEST_CHECK(!after.empty() && after.front()->getVertexGeometryLayoutParameters().skirtOffset >= 0, "and the next build uses the new one");

    builder.setTransformer(planar);
    std::vector<std::shared_ptr<TileSurface>> back = builder.buildTileSurface(tileId);
    TEST_CHECK(!back.empty() && !before.empty() && back.front()->getVertexGeometry().size() == before.front()->getVertexGeometry().size()
               && back.front()->getVertexGeometryLayoutParameters().skirtOffset < 0, "swapping back builds what the original builder built");
}
