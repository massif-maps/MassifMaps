// A composite decodes one tile once per layer. They share one parsed tile (MBVTTileCache), the
// decoder reads the same features through it as through its own parse, features with equal tags
// share one FeatureData, and a Style answers "elevation-mode" from a cache.
//
// Not covered: MBVectorTileDecoder::createFeatureDecoder, which links the whole decoder - that the
// layers of a composite hit the cache, and the decode time it saves, are a device check.

#include "TestCheck.h"

#include <mapnikvt/Filter.h>
#include <mapnikvt/Logger.h>
#include <mapnikvt/MBVTFeatureDecoder.h>
#include <mapnikvt/Properties.h>
#include <mapnikvt/Rule.h>
#include <mapnikvt/Style.h>
#include <mapnikvt/Symbolizer.h>
#include <mbvtbuilder/MBVTTileBuilder.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mvt = massif::mvt;
namespace builder = massif::mbvtbuilder;

namespace {
    struct NullLogger final : mvt::Logger {
        void write(Severity, const std::string&) override { }
    };

    std::vector<unsigned char> makeTile(int roads) {
        builder::MBVTTileBuilder tileBuilder(0, 14);
        int layer = tileBuilder.createLayer("road");
        for (int i = 0; i < roads; i++) {
            picojson::object props;
            props["class"] = picojson::value(std::string(i % 2 ? "b" : "a"));
            double lat = 10.0 + i * 0.5;
            tileBuilder.addMultiLineString(layer, { { { -60.0, lat }, { 60.0, lat + 1.0 } } }, picojson::value(static_cast<double>(i)), picojson::value(props), false);
        }
        protobuf::encoded_message encoded;
        tileBuilder.buildTile(0, 0, 0, encoded);
        return std::vector<unsigned char>(encoded.data().begin(), encoded.data().end());
    }

    struct Read {
        std::vector<std::shared_ptr<const mvt::FeatureData>> data;
        std::vector<std::vector<cglib::vec2<float>>> lines;
        std::vector<std::string> classes;
    };

    Read readRoads(mvt::MBVTFeatureDecoder& decoder) {
        Read read;
        decoder.setTransform(cglib::mat3x3<float>::identity());
        std::set<std::string> fields { "class" };
        auto it = decoder.createLayerFeatureIterator("road", &fields);
        for (; it && it->valid(); it->advance()) {
            if (auto line = std::get_if<mvt::LineGeometry>(it->getGeometry().get())) {
                read.lines.push_back(line->getVerticesList().front());
            }
            auto data = it->getFeatureData(false, &fields);
            mvt::Value value;
            data->getVariable("class", value);
            read.classes.push_back(std::get<std::string>(value));
            read.data.push_back(data);
        }
        return read;
    }

    struct ElevationSymbolizer : mvt::Symbolizer {
        explicit ElevationSymbolizer(bool set) : Symbolizer(std::make_shared<NullLogger>()) {
            bindProperty("elevation-mode", &_elevationMode);
            if (set) {
                getProperty("elevation-mode")->setExpression(mvt::Value(std::string("span")));
            }
        }

        FeatureProcessor createFeatureProcessor(const mvt::ExpressionContext&, const mvt::SymbolizerContext&) const override {
            return FeatureProcessor();
        }

        mvt::StringProperty _elevationMode = mvt::StringProperty("drape");
    };

    mvt::Style makeStyle(bool elevationMode) {
        auto rule = std::make_shared<mvt::Rule>("r", 0, 22, std::make_shared<mvt::Filter>(mvt::Filter::Type::FILTER, std::nullopt),
                                                std::vector<std::shared_ptr<const mvt::Symbolizer>> { std::make_shared<ElevationSymbolizer>(elevationMode) });
        return mvt::Style("s", 1.0f, std::string(), std::nullopt, mvt::Style::FilterMode::ALL, std::string(), { rule });
    }
}

void testTileParseShare() {
    std::vector<unsigned char> bytes = makeTile(6);
    auto logger = std::make_shared<NullLogger>();

    mvt::MBVTFeatureDecoder own(bytes, logger);
    mvt::MBVTFeatureDecoder shared(mvt::MBVTFeatureDecoder::parseTile(bytes), logger);
    Read a = readRoads(own);
    Read b = readRoads(shared);
    TEST_CHECK(a.lines.size() == 6 && a.lines == b.lines && a.classes == b.classes, "a decoder on a shared parsed tile reads the same geometry and fields as one parsing its own");

    TEST_CHECK(a.data[0] == a.data[2] && a.data[1] == a.data[3] && a.data[0] == a.data[4], "features with equal tags share one FeatureData");
    TEST_CHECK(a.data[0] != a.data[1] && a.classes[0] == "a" && a.classes[1] == "b", "features with other tags do not");

    mvt::MBVTTileCache cache(2);
    std::atomic<int> parses { 0 };
    auto parse = [&](const std::vector<unsigned char>& data) {
        return [&parses, &data]() { parses++; return mvt::MBVTFeatureDecoder::parseTile(data); };
    };
    auto first = cache.get(bytes, parse(bytes));
    auto second = cache.get(std::vector<unsigned char>(bytes), parse(bytes));
    TEST_CHECK(first && first == second && parses == 1, "equal bytes are parsed once and the tile is shared");

    std::vector<unsigned char> other = makeTile(3);
    std::vector<unsigned char> sameSize = bytes;
    sameSize[sameSize.size() / 2] ^= 0x01;
    TEST_CHECK(cache.get(other, parse(other)) != first && parses == 2, "other bytes are parsed on their own");
    cache.get(sameSize, [&]() { parses++; return std::shared_ptr<const vector_tile::Tile>(); });
    TEST_CHECK(parses == 3, "bytes of the same size with one bit changed are not mistaken for the cached tile");

    cache.get(bytes, parse(bytes));
    TEST_CHECK(parses == 4, "the oldest entry is dropped beyond the capacity");

    int nullParses = 0;
    std::vector<unsigned char> mlt { 1, 2, 3, 4 };
    cache.get(mlt, [&]() { nullParses++; return std::shared_ptr<const vector_tile::Tile>(); });
    TEST_CHECK(!cache.get(mlt, [&]() { nullParses++; return std::shared_ptr<const vector_tile::Tile>(); }) && nullParses == 1, "a tile the parser declines (null) is remembered as declined");

    std::vector<unsigned char> broken { 9, 9 };
    bool threw = false;
    try {
        cache.get(broken, []() -> std::shared_ptr<const vector_tile::Tile> { throw std::runtime_error("bad tile"); });
    }
    catch (const std::runtime_error&) {
        threw = true;
    }
    int retries = 0;
    cache.get(broken, [&]() { retries++; return mvt::MBVTFeatureDecoder::parseTile(bytes); });
    TEST_CHECK(threw && retries == 1, "a parse that throws is not remembered: the next caller parses again");

    mvt::MBVTTileCache busy(4);
    std::atomic<int> slowParses { 0 };
    std::vector<std::shared_ptr<const vector_tile::Tile>> results(8);
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) {
        threads.emplace_back([&, i]() {
            results[i] = busy.get(bytes, [&]() {
                slowParses++;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return mvt::MBVTFeatureDecoder::parseTile(bytes);
            });
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    bool allSame = true;
    for (auto& result : results) {
        allSame = allSame && result && result == results[0];
    }
    TEST_CHECK(slowParses == 1 && allSame, "layers asking at once wait for the one parse instead of each running their own");

    mvt::Style plain = makeStyle(false);
    mvt::Style lifting = makeStyle(true);
    TEST_CHECK(!plain.hasElevationMode() && !plain.hasElevationMode(), "a style no symbolizer of which sets elevation-mode has none, asked twice");
    TEST_CHECK(lifting.hasElevationMode() && lifting.hasElevationMode(), "a style with a symbolizer that sets it has it, asked twice");
}
