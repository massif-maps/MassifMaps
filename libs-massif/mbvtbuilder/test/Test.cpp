#define BOOST_TEST_MODULE GEOJSONVT

#include "Clipper.h"
#include "Simplifier.h"
#include "MBVTLayerEncoder.h"
#include "MBVTTileBuilder.h"

#include "mapnikvt/mbvtpackage/MBVTPackage.pb.h"

#include <picojson/picojson.h>

#include <boost/math/constants/constants.hpp>
#include <boost/test/included/unit_test.hpp>

using namespace carto::mbvtbuilder;

static std::vector<std::vector<cglib::vec2<float>>> decodeGeometry(const vector_tile::Tile::Feature& feature, float scale = 1.0f / 4096.0f) {
    std::vector<std::vector<cglib::vec2<float>>> verticesList;
    int cx = 0, cy = 0;
    int cmd = 0, length = 0;
    std::vector<cglib::vec2<float>> vertices;
    vertices.reserve(feature.geometry_size());
    for (int i = 0; i < feature.geometry_size(); ) {
        if (length == 0) {
            int cmdLength = feature.geometry(i++);
            length = cmdLength >> 3;
            cmd = cmdLength & 7;
            if (length == 0) {
                continue;
            }
        }

        length--;
        if ((cmd == 1 || cmd == 2) && i + 2 <= feature.geometry_size()) {
            if (cmd == 1) {
                if (!vertices.empty()) {
                    verticesList.emplace_back();
                    std::swap(verticesList.back(), vertices);
                }
            }
            int dx = feature.geometry(i++);
            int dy = feature.geometry(i++);
            dx = ((dx >> 1) ^ (-(dx & 1)));
            dy = ((dy >> 1) ^ (-(dy & 1)));
            cx += dx;
            cy += dy;
            vertices.emplace_back(static_cast<float>(cx) * scale, static_cast<float>(cy) * scale);
        }
        else if (cmd == 7) {
            if (!vertices.empty()) {
                if (vertices.front() != vertices.back()) {
                    cglib::vec2<float> p = vertices.front();
                    vertices.emplace_back(p);
                }
            }
        }
    }
    if (!vertices.empty()) {
        verticesList.emplace_back();
        std::swap(verticesList.back(), vertices);
    }

    return verticesList;
}

static cglib::vec2<double> wgs84ToWM(const cglib::vec2<double>& posWgs84) {
    static constexpr double EARTH_RADIUS = 6378137.0;
    static constexpr double PI = boost::math::constants::pi<double>();
    
    double x = EARTH_RADIUS * posWgs84(0) * PI / 180.0;
    double a = posWgs84(1) * PI / 180.0;
    double y = 0.5 * EARTH_RADIUS * std::log((1.0 + std::sin(a)) / (1.0 - std::sin(a)));
    return cglib::vec2<double>(x, y);
}

// Test clipper for different primitives
BOOST_AUTO_TEST_CASE(clipper) {
    typedef cglib::vec2<double> Point;

    Clipper<double> clipper(cglib::bbox2<double>({ -1.0,-1.0 }, { 1.0, 1.0 }));

    {
        BOOST_CHECK(clipper.testPoint({ -0.9, 0.9 }));
        BOOST_CHECK(clipper.testPoint({ -1, 1 }));
        BOOST_CHECK(!clipper.testPoint({ -1.1, 1 }));
    }

    {
        BOOST_CHECK((clipper.clipLineString(std::vector<Point> { { -1, 0 }, { 1, 0 } }) == std::vector<std::vector<Point>> { { { -1, 0 }, { 1, 0 } } }));
        BOOST_CHECK((clipper.clipLineString(std::vector<Point> { { -2, 0 }, { 2, 0 } }) == std::vector<std::vector<Point>> { { { -1, 0 }, { 1, 0 } } }));
        BOOST_CHECK((clipper.clipLineString(std::vector<Point> { { -2, -2 }, { 2, 2 } }) == std::vector<std::vector<Point>> { { { -1, -1 }, { 1, 1 } } }));;
        BOOST_CHECK((clipper.clipLineString(std::vector<Point> { { -4, 0 }, { 0, 2 } }) == std::vector<std::vector<Point>> { }));
        BOOST_CHECK((clipper.clipLineString(std::vector<Point> { { -4, 0 }, { 0, 0 }, { 4, 0.5 }, { 0, 0.5 } }) == std::vector<std::vector<Point>> { { { -1, 0 }, { 0, 0 }, { 1, 0.125 } }, { { 1, 0.5 }, { 0, 0.5 } } }));
    }

    {
        BOOST_CHECK((clipper.clipPolygonRing(std::vector<Point> { { -1, 0 }, { 1, 0 }, { 0, 1 } }) == std::vector<Point> { { -1, 0 }, { 1, 0 }, { 0, 1 } }));
        BOOST_CHECK((clipper.clipPolygonRing(std::vector<Point> { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } }) == std::vector<Point> { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } }));
        BOOST_CHECK((clipper.clipPolygonRing(std::vector<Point> { { -2, -2 }, { -2, 2 }, { 2, 2 }, { 2, -2 } }) == std::vector<Point> { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } }));
        BOOST_CHECK((clipper.clipPolygonRing(std::vector<Point> { { -2, 0 }, { 0, 2 }, { 2, 0 }, { 0, -2 } }) == std::vector<Point> { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 }, { 0, -1 } }));
    }
}

// Test simplifier for line strings
BOOST_AUTO_TEST_CASE(simplifier) {
    typedef cglib::vec2<double> Point;

    Simplifier<double> simplifier(1.0);

    {
        BOOST_CHECK((simplifier.simplifyLineString(std::vector<Point> { { -1, 0 }, { 1, 0 } }) == std::vector<Point> { { -1, 0 }, { 1, 0 } }));
        BOOST_CHECK((simplifier.simplifyLineString(std::vector<Point> { { -1, 0 }, { 1, 0 }, { 2, 0 } }) == std::vector<Point> { { -1, 0 }, { 2, 0 } }));
        BOOST_CHECK((simplifier.simplifyLineString(std::vector<Point> { { -1, 0 }, { 1, 1 }, { 2, 0 } }) == std::vector<Point> { { -1, 0 }, { 1, 1 }, { 2, 0 } }));
        BOOST_CHECK((simplifier.simplifyLineString(std::vector<Point> { { -1, 0 }, { 1, 2 }, { 2, 0 } }) == std::vector<Point> { { -1, 0 }, { 1, 2 }, { 2, 0 } }));
        BOOST_CHECK((simplifier.simplifyLineString(std::vector<Point> { { -1, 0 }, { 1, 0.5 }, { 2, 0 } }) == std::vector<Point> { { -1, 0 }, { 2, 0 } }));
    }
}

// Test that properties are encoded correctly in vector tiles
BOOST_AUTO_TEST_CASE(propertiesEncoding) {
    std::vector<vector_tile::Tile_Layer> layers;

    auto encodeAndDecodeValue = [&layers](const picojson::value& value) -> vector_tile::Tile_Value {
        MBVTLayerEncoder encoder("");
        encoder.addMultiPoint({ cglib::vec2<float>(0, 0) }, picojson::value(picojson::object({ { "_key_", value } })));
        auto encodedMsg = encoder.buildLayer();
        protobuf::message msg(encodedMsg.data().data(), encodedMsg.data().size());
        layers.emplace_back(msg);
        BOOST_ASSERT(layers.back().keys().at(0) == "_key_");
        return layers.back().values().at(0);
    };

    auto encodeAndDecodeKeyValue = [&layers](const std::string& key, const picojson::value& value) -> std::pair<std::string, std::string> {
        MBVTLayerEncoder encoder("");
        encoder.addMultiPoint({ cglib::vec2<float>(0, 0) }, picojson::value(picojson::object({ { key, value } })));
        auto encodedMsg = encoder.buildLayer();
        protobuf::message msg(encodedMsg.data().data(), encodedMsg.data().size());
        layers.emplace_back(msg);
        BOOST_ASSERT(layers.back().values().at(0).has_string_value());
        return std::make_pair(layers.back().keys().at(0), layers.back().values().at(0).string_value());
    };

    BOOST_CHECK(encodeAndDecodeValue(picojson::value(false)).bool_value() == false);
    BOOST_CHECK(encodeAndDecodeValue(picojson::value(true)).bool_value() == true);
    BOOST_CHECK(encodeAndDecodeValue(picojson::value(1LL)).uint_value() == 1);
    BOOST_CHECK(encodeAndDecodeValue(picojson::value(-1LL)).sint_value() == -1);
    BOOST_CHECK(encodeAndDecodeValue(picojson::value(-1564564561LL)).sint_value() == -1564564561);
    BOOST_CHECK(encodeAndDecodeValue(picojson::value(91564564561LL)).uint_value() == 91564564561);
    BOOST_CHECK(encodeAndDecodeValue(picojson::value(16.25)).float_value() == 16.25f);
    BOOST_CHECK(encodeAndDecodeValue(picojson::value(99916.251111111)).double_value() == 99916.251111111);
    BOOST_CHECK(encodeAndDecodeValue(picojson::value("Test str")).string_value() == "Test str");

    BOOST_CHECK((encodeAndDecodeKeyValue("key1", picojson::value(picojson::object { { "a", picojson::value("XX") } })) == std::make_pair(std::string("key1.a"), std::string("XX"))));
    BOOST_CHECK((encodeAndDecodeKeyValue("key2", picojson::value(picojson::array { { picojson::value("XX") } })) == std::make_pair(std::string("key2[0]"), std::string("XX"))));
}

// Test that point geometry is encoded correctly in vector tiles
BOOST_AUTO_TEST_CASE(pointEncoding) {
    typedef cglib::vec2<float> Point;

    auto encodeAndDecodeGeometry = [](const std::vector<Point>& geom) -> std::vector<Point> {
        MBVTLayerEncoder encoder("");
        encoder.addMultiPoint(geom, picojson::value());
        auto encodedMsg = encoder.buildLayer();
        protobuf::message msg(encodedMsg.data().data(), encodedMsg.data().size());
        vector_tile::Tile_Layer layer(msg);
        BOOST_ASSERT(layer.features_size() == 1);
        auto decodedGeom = decodeGeometry(layer.features(0));
        std::vector<Point> points;
        for (auto& decodedPoint : decodedGeom) {
            BOOST_ASSERT(decodedPoint.size() == 1);
            points.push_back(decodedPoint.at(0));
        }
        return points;
    };
    
    BOOST_CHECK((encodeAndDecodeGeometry({ Point(0, 0) }) == std::vector<Point> { Point(0, 0) }));
    BOOST_CHECK((encodeAndDecodeGeometry({ Point(0, 1) }) == std::vector<Point> { Point(0, 1) }));
    BOOST_CHECK((encodeAndDecodeGeometry({ Point(-0.125f, 1.25f) }) == std::vector<Point> { Point(-0.125f, 1.25f) }));
    BOOST_CHECK((encodeAndDecodeGeometry({ Point(-0.125f, 1.25f), Point(-0.125f, 1.25f) }) == std::vector<Point> { Point(-0.125f, 1.25f), Point(-0.125f, 1.25f) }));
    BOOST_CHECK((encodeAndDecodeGeometry({ Point(-0.125f, 1.25f), Point(0.125f, -1.25f) }) == std::vector<Point> { Point(-0.125f, 1.25f), Point(0.125f, -1.25f) }));
}

// Test that linestring geometry is encoded correctly in vector tiles
BOOST_AUTO_TEST_CASE(lineStringEncoding) {
    typedef cglib::vec2<float> Point;

    auto encodeAndDecodeGeometry = [](const std::vector<Point>& geom) -> std::vector<Point> {
        MBVTLayerEncoder encoder("");
        encoder.addMultiLineString({ geom }, picojson::value());
        auto encodedMsg = encoder.buildLayer();
        protobuf::message msg(encodedMsg.data().data(), encodedMsg.data().size());
        vector_tile::Tile_Layer layer(msg);
        BOOST_ASSERT(layer.features_size() == 1);
        auto decodedGeom = decodeGeometry(layer.features(0));
        BOOST_ASSERT(decodedGeom.size() == 1);
        return decodedGeom.at(0);
    };

    BOOST_CHECK((encodeAndDecodeGeometry({ Point(0, 0), Point(1, 0) }) == std::vector<Point> { Point(0, 0), Point(1, 0) }));
    BOOST_CHECK((encodeAndDecodeGeometry({ Point(0, 0), Point(1, 0), Point(0, 1) }) == std::vector<Point> { Point(0, 0), Point(1, 0), Point(0, 1) }));
    BOOST_CHECK((encodeAndDecodeGeometry({ Point(0, 0), Point(1, 0), Point(1, 0) }) == std::vector<Point> { Point(0, 0), Point(1, 0) }));
    BOOST_CHECK((encodeAndDecodeGeometry({ Point(-0.125f, 1.25f), Point(0.125f, -1.25f) }) == std::vector<Point> { Point(-0.125f, 1.25f), Point(0.125f, -1.25f) }));
}

// Test that polygon geometry is encoded correctly in vector tiles
BOOST_AUTO_TEST_CASE(polygonEncoding) {
    typedef cglib::vec2<float> Point;

    auto encodeAndDecodeGeometry = [](const std::vector<std::vector<Point>>& geom) -> std::vector<std::vector<Point>> {
        MBVTLayerEncoder encoder("");
        encoder.addMultiPolygon(geom, picojson::value());
        auto encodedMsg = encoder.buildLayer();
        protobuf::message msg(encodedMsg.data().data(), encodedMsg.data().size());
        vector_tile::Tile_Layer layer(msg);
        BOOST_ASSERT(layer.features_size() == 1);
        auto decodedGeom = decodeGeometry(layer.features(0));
        BOOST_ASSERT(decodedGeom.size() == 1);
        return decodedGeom;
    };

    BOOST_CHECK((encodeAndDecodeGeometry({ { Point(0, 0), Point(1, 0), Point(0, 1) } }) == std::vector<std::vector<Point>> { { Point(0, 0), Point(1, 0), Point(0, 1), Point(0, 0) } }));
    BOOST_CHECK((encodeAndDecodeGeometry({ { Point(0, 0), Point(1, 0), Point(1, 0), Point(0, 1) } }) == std::vector<std::vector<Point>> { { Point(0, 0), Point(1, 0), Point(0, 1), Point(0, 0) } }));
    BOOST_CHECK((encodeAndDecodeGeometry({ { Point(1, 1), Point(-0.125f, 1.25f), Point(0.125f, -1.25f) } }) == std::vector<std::vector<Point>> { { Point(1, 1), Point(-0.125f, 1.25f), Point(0.125f, -1.25f), Point(1, 1) } }));
}

// Test high-level tile builder functionality
BOOST_AUTO_TEST_CASE(tileBuilder) {
    typedef cglib::vec2<double> Point;

    {
        MBVTTileBuilder tileBuilder(18, 18);
        tileBuilder.addMultiPoint(0, { Point(0, 0) }, picojson::value());
        std::vector<protobuf::encoded_message> encodedTiles;
        tileBuilder.buildTiles([&encodedTiles](int zoom, int tileX, int tileY, const protobuf::encoded_message& encodedTile) { encodedTiles.push_back(encodedTile); });
        BOOST_CHECK(encodedTiles.size() == 1);
    }

    {
        MBVTTileBuilder tileBuilder(18, 18);
        tileBuilder.addMultiPoint(0, { Point(0, 0), Point(1, 1) }, picojson::value());
        std::vector<protobuf::encoded_message> encodedTiles;
        tileBuilder.buildTiles([&encodedTiles](int zoom, int tileX, int tileY, const protobuf::encoded_message& encodedTile) { encodedTiles.push_back(encodedTile); });
        BOOST_CHECK(encodedTiles.size() == 1);
    }

    {
        MBVTTileBuilder tileBuilder(18, 18);
        auto layerIndex = tileBuilder.createLayer("", 0.0000001f);
        tileBuilder.addMultiPoint(layerIndex, { Point(0.5, 0.5), Point(100000, 1) }, picojson::value());
        std::vector<protobuf::encoded_message> encodedTiles;
        tileBuilder.buildTiles([&encodedTiles](int zoom, int tileX, int tileY, const protobuf::encoded_message& encodedTile) { encodedTiles.push_back(encodedTile); });
        BOOST_CHECK(encodedTiles.size() == 2);
    }

    {
        MBVTTileBuilder tileBuilder(18, 18);
        auto layerIndex = tileBuilder.createLayer("", 0.0000001f);
        tileBuilder.addMultiLineString(layerIndex, { { Point(0.5, 0.5), Point(100000, 1) } }, picojson::value());
        std::vector<protobuf::encoded_message> encodedTiles;
        tileBuilder.buildTiles([&encodedTiles](int zoom, int tileX, int tileY, const protobuf::encoded_message& encodedTile) { encodedTiles.push_back(encodedTile); });
        BOOST_CHECK(encodedTiles.size() == 655);
    }

    {
        MBVTTileBuilder tileBuilder(18, 18);
        auto layerIndex = tileBuilder.createLayer("", 0.0000001f);
        tileBuilder.addMultiLineString(layerIndex, { { Point(0.5, 0.5), Point(10000, 10000) } }, picojson::value());
        std::vector<protobuf::encoded_message> encodedTiles;
        tileBuilder.buildTiles([&encodedTiles](int zoom, int tileX, int tileY, const protobuf::encoded_message& encodedTile) { encodedTiles.push_back(encodedTile); });
        BOOST_CHECK(encodedTiles.size() == 196);
    }

    {
        MBVTTileBuilder tileBuilder(18, 18);
        auto layerIndex = tileBuilder.createLayer("", 0.0000001f);
        tileBuilder.addMultiPolygon(layerIndex, { { { Point(0.5, 0.5), Point(0.5, 10000), Point(10000, 5000) } } }, picojson::value());
        std::vector<protobuf::encoded_message> encodedTiles;
        tileBuilder.buildTiles([&encodedTiles](int zoom, int tileX, int tileY, const protobuf::encoded_message& encodedTile) { encodedTiles.push_back(encodedTile); });
        BOOST_CHECK(encodedTiles.size() == 2211);
    }
}

// Test high-level tile builder functionality, simplifier part
BOOST_AUTO_TEST_CASE(tileBuilderSimplifier) {
    typedef cglib::vec2<double> Point;

    {
        MBVTTileBuilder tileBuilder(0, 18);
        std::vector<Point> coords;
        for (int i = 0; i < 10000; i++) {
            double a = static_cast<double>(i) / 10000 * std::atan(1.0) * 8;
            double r = 80;
            coords.emplace_back(wgs84ToWM(cglib::vec2<double>(std::cos(a), std::sin(a)) * r));
        }
        tileBuilder.addMultiPolygon(0, { { coords } }, picojson::value());
        protobuf::encoded_message encodedTile;
        tileBuilder.buildTile(0, 0, 0, encodedTile);
    }
}
