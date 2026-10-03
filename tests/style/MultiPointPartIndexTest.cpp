// Sources merge many points into one MultiPoint that keeps one feature id; each point's label id is
// that id plus its part index. Overzoomed, the decoder drops the parts outside the target tile, so
// the index has to stay the one in the source feature: a post-clip index gave different points of
// sibling tiles one label id, and the renderer merged them into one label.
// Not covered: the symbolizers that build the id (they link vt), and the MLT decoder (it links mlt);
// both read the index through PointGeometry::getPartIndex and mbvtKeepPointParts, checked here.

#include "TestCheck.h"

#include <mapnikvt/Logger.h>
#include <mapnikvt/MBVTFeatureDecoder.h>
#include <mapnikvt/MBVTGeometryBounds.h>
#include <mapnikvt/MBVTSubtile.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace mvt = massif::mvt;
namespace detail = massif::mvt::subtile_detail;

namespace {
    struct NullLogger final : mvt::Logger {
        void write(Severity, const std::string&) override { }
    };

    constexpr int EXTENT = 4096;
    constexpr long long FEATURE_ID = 4242;

    // Unevenly placed, so a dropped part shifts every index after it.
    const std::vector<std::pair<int, int>> POINTS = {
        { 1000, 1000 }, // top-left child only
        { 3000, 1000 }, // top-right child only
        { 2100, 1000 }, // top-right child, and the top-left child's clip buffer
        { 3000, 3000 }, // bottom-right child only
    };

    std::vector<unsigned char> multiPointTile() {
        std::string geometry;
        detail::writeVarint(geometry, (POINTS.size() << 3) | 1);
        int x = 0, y = 0;
        for (const auto& point : POINTS) {
            detail::writeVarint(geometry, detail::zigzag(point.first - x));
            detail::writeVarint(geometry, detail::zigzag(point.second - y));
            x = point.first;
            y = point.second;
        }
        std::string feature;
        detail::writeVarint(feature, 1 << 3);
        detail::writeVarint(feature, FEATURE_ID);
        detail::writeVarint(feature, 3 << 3);
        detail::writeVarint(feature, 1); // POINT
        detail::writeBytes(feature, 4, geometry);

        std::string layer;
        detail::writeVarint(layer, 15 << 3);
        detail::writeVarint(layer, 2);
        detail::writeBytes(layer, 1, "trees");
        detail::writeBytes(layer, 2, feature);
        detail::writeVarint(layer, 5 << 3);
        detail::writeVarint(layer, EXTENT);
        std::string tile;
        detail::writeBytes(tile, 3, layer);
        return std::vector<unsigned char>(tile.begin(), tile.end());
    }

    struct Part {
        int sourcePoint; // which of POINTS the vertex is, -1 for none
        int partIndex;
    };

    // The parts the decoder yields for child (x, y) one zoom under the source; identity when dz is 0.
    std::vector<Part> decodeParts(int dz, int x, int y, long long& featureId) {
        mvt::MBVTFeatureDecoder decoder(multiPointTile(), std::make_shared<NullLogger>());
        float scale = static_cast<float>(1 << dz);
        decoder.setTransform(cglib::translate3_matrix(cglib::vec3<float>(-x, -y, 1)) * cglib::scale3_matrix(cglib::vec3<float>(scale, scale, 1)));
        decoder.setClipBox(cglib::bbox2<float>(cglib::vec2<float>(-0.125f, -0.125f), cglib::vec2<float>(1.125f, 1.125f)));

        std::vector<Part> parts;
        featureId = 0;
        auto it = decoder.createLayerFeatureIterator("trees", nullptr);
        for (; it && it->valid(); it->advance()) {
            featureId = it->getFeatureId();
            auto pointGeometry = std::get_if<mvt::PointGeometry>(it->getGeometry().get());
            if (!pointGeometry) {
                continue;
            }
            for (std::size_t part = 0; part < pointGeometry->getVerticesList().size(); part++) {
                for (const cglib::vec2<float>& vertex : pointGeometry->getVerticesList()[part]) {
                    int sourcePoint = -1;
                    for (std::size_t i = 0; i < POINTS.size(); i++) {
                        float sourceX = (vertex(0) + x) / scale * EXTENT;
                        float sourceY = (vertex(1) + y) / scale * EXTENT;
                        if (std::abs(sourceX - POINTS[i].first) < 0.5f && std::abs(sourceY - POINTS[i].second) < 0.5f) {
                            sourcePoint = static_cast<int>(i);
                        }
                    }
                    parts.push_back(Part { sourcePoint, pointGeometry->getPartIndex(part) });
                }
            }
        }
        return parts;
    }

    bool indicesAreSource(const std::vector<Part>& parts) {
        for (const Part& part : parts) {
            if (part.sourcePoint < 0 || part.partIndex != part.sourcePoint) {
                return false;
            }
        }
        return !parts.empty();
    }

    int indexOf(const std::vector<Part>& parts, int sourcePoint) {
        for (const Part& part : parts) {
            if (part.sourcePoint == sourcePoint) {
                return part.partIndex;
            }
        }
        return -1;
    }
}

void testMultiPointPartIndex() {
    long long sourceId = 0, topLeftId = 0, topRightId = 0, bottomRightId = 0;
    std::vector<Part> source = decodeParts(0, 0, 0, sourceId);
    std::vector<Part> topLeft = decodeParts(1, 0, 0, topLeftId);
    std::vector<Part> topRight = decodeParts(1, 1, 0, topRightId);
    std::vector<Part> bottomRight = decodeParts(1, 1, 1, bottomRightId);

    TEST_CHECK(source.size() == POINTS.size() && indicesAreSource(source), "the source tile keeps every point, indexed in order");
    TEST_CHECK(topLeft.size() == 2 && topRight.size() == 2 && bottomRight.size() == 1, "an overzoomed tile keeps only the points its clip reaches");
    TEST_CHECK(sourceId == FEATURE_ID && topLeftId == FEATURE_ID && topRightId == FEATURE_ID && bottomRightId == FEATURE_ID,
               "every tile reads the one feature id, so only the part index tells the points apart");

    TEST_CHECK(indicesAreSource(topLeft) && indicesAreSource(topRight) && indicesAreSource(bottomRight),
               "an overzoomed point keeps its index in the source feature");
    TEST_CHECK(indexOf(topLeft, 0) != indexOf(topRight, 1) && indexOf(topRight, 1) != indexOf(bottomRight, 3),
               "the first point kept by sibling tiles is not one index, so not one label");
    TEST_CHECK(indexOf(topLeft, 2) == 2 && indexOf(topRight, 2) == 2, "a point in two tiles' clip has one index in both");

    std::vector<std::vector<cglib::vec2<float>>> verticesList = { { { 0.0f, 0.0f } }, { { 5.0f, 0.0f } }, { { 0.5f, 0.0f } } };
    auto missesUnitBox = [](const std::vector<cglib::vec2<float>>& vertices) { return vertices.front()(0) > 1.0f; };
    std::vector<int> partIndices = mvt::mbvtKeepPointParts(verticesList, missesUnitBox);
    TEST_CHECK(verticesList.size() == 2 && verticesList[1].front()(0) == 0.5f && partIndices == std::vector<int>({ 0, 2 }),
               "dropping a part moves the next one down and records where it came from");
    TEST_CHECK(mvt::mbvtKeepPointParts(verticesList, missesUnitBox).empty(), "nothing dropped, nothing recorded: a lone point pays no index");
}
