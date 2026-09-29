// MergedMBVTTileDataSource cuts a source's last tile into the child tile past its max zoom
// (MBVTSubtile.h): the bathymetry archive stops at z6 while the low-zoom landcover draws to z8.

#include "TestCheck.h"

#include <mapnikvt/MBVTSubtile.h>
#include <mapnikvt/mbvtpackage/MBVTPackage.pb.h>

#include <string>
#include <vector>

namespace mvt = massif::mvt;
namespace detail = massif::mvt::subtile_detail;

namespace {
    std::string feature(std::uint64_t type, const std::vector<std::int64_t>& moveTo, const std::vector<std::int64_t>& lineTo) {
        std::string geometry;
        detail::writeVarint(geometry, (1 << 3) | 1);
        detail::writeVarint(geometry, detail::zigzag(moveTo[0]));
        detail::writeVarint(geometry, detail::zigzag(moveTo[1]));
        if (!lineTo.empty()) {
            detail::writeVarint(geometry, ((lineTo.size() / 2) << 3) | 2);
            for (std::int64_t delta : lineTo) {
                detail::writeVarint(geometry, detail::zigzag(delta));
            }
        }
        std::string out;
        detail::writeVarint(out, 3 << 3);
        detail::writeVarint(out, type);
        detail::writeBytes(out, 4, geometry);
        return out;
    }

    vector_tile::Tile parse(const std::vector<unsigned char>& data) {
        return vector_tile::Tile(protobuf::message(data.data(), data.size()));
    }
}

void testMBVTSubtile() {
    std::string layer;
    detail::writeBytes(layer, 1, "landcover");
    detail::writeBytes(layer, 2, feature(2, { 1024, 1024 }, { 2048, 0, 0, 2048 })); // (1024,1024) to (3072,3072)
    detail::writeBytes(layer, 2, feature(1, { 100, 100 }, {}));
    detail::writeVarint(layer, 5 << 3);
    detail::writeVarint(layer, 4096);
    std::string tile;
    detail::writeBytes(tile, 3, layer);
    std::vector<unsigned char> parent(tile.begin(), tile.end());

    // z+1, the bottom-right child: the line moves by half the tile, the point falls outside
    vector_tile::Tile child = parse(mvt::subtileMBVT(parent, 1, 7, 9));
    TEST_CHECK(child.layers_size() == 1 && child.layers(0).name() == "landcover", "the layer is kept, named");
    TEST_CHECK(child.layers(0).extent() == 2048, "the extent is halved: a parent unit is a child's");
    TEST_CHECK(child.layers(0).features_size() == 1, "the point in the other quarter is dropped");
    const auto& geometry = child.layers(0).features(0).geometry();
    TEST_CHECK(geometry.size() == 8 && detail::unzigzag(geometry[1]) == -1024 && detail::unzigzag(geometry[2]) == -1024,
               "only the first MoveTo moves");
    TEST_CHECK(detail::unzigzag(geometry[4]) == 2048, "the LineTo deltas are left as they were");

    // two zooms up, the top-left child keeps the point
    vector_tile::Tile corner = parse(mvt::subtileMBVT(parent, 2, 0, 0));
    TEST_CHECK(corner.layers(0).extent() == 1024 && corner.layers(0).features_size() == 2, "both features reach the top-left quarter");

    TEST_CHECK(mvt::subtileMBVT(parent, 13, 0, 0).empty(), "an extent that does not divide is refused");
}
