// A line gradient needs each tile piece of a line to know which part of the whole line it is. The
// GeoJSON tile builder tags a piece with that range when line metrics are on, and the MVT decoder
// hands it to the geometry. Checked end to end, builder to decoder. Not covered: what a style does
// with the range (the symbolizers link vt).

#include "TestCheck.h"

#include <mapnikvt/Logger.h>
#include <mapnikvt/MBVTFeatureDecoder.h>
#include <mbvtbuilder/MBVTTileBuilder.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mvt = massif::mvt;
namespace builder = massif::mbvtbuilder;

namespace {
    struct NullLogger final : mvt::Logger {
        void write(Severity, const std::string&) override { }
    };

    using Progress = std::optional<mvt::LineGeometry::Progress>;

    struct LineRead {
        std::vector<Progress> progress;
        bool progressIsAField = false;
    };

    // An east-west line over 120 degrees, off the equator so no tile edge runs along it. At zoom 2 it
    // is cut at longitude 0.
    std::unique_ptr<builder::MBVTTileBuilder> makeBuilder(bool lineMetrics) {
        auto tileBuilder = std::make_unique<builder::MBVTTileBuilder>(0, 14);
        tileBuilder->setLineMetrics(lineMetrics);
        int layer = tileBuilder->createLayer("route");
        tileBuilder->addMultiLineString(layer, { { { -60.0, 10.0 }, { 60.0, 10.0 } } }, picojson::value(), picojson::value(picojson::object()), false);
        return tileBuilder;
    }

    LineRead readTile(const builder::MBVTTileBuilder& tileBuilder, int zoom, int x, int y) {
        LineRead result;
        protobuf::encoded_message encoded;
        tileBuilder.buildTile(zoom, x, y, encoded);
        if (encoded.empty()) {
            return result;
        }

        mvt::MBVTFeatureDecoder decoder(std::vector<unsigned char>(encoded.data().begin(), encoded.data().end()), std::make_shared<NullLogger>());
        decoder.setTransform(cglib::mat3x3<float>::identity());
        decoder.setClipBox(cglib::bbox2<float>(cglib::vec2<float>(-0.125f, -0.125f), cglib::vec2<float>(1.125f, 1.125f)));
        auto it = decoder.createLayerFeatureIterator("route", nullptr);
        for (; it && it->valid(); it->advance()) {
            if (auto line = std::get_if<mvt::LineGeometry>(it->getGeometry().get())) {
                result.progress.push_back(line->getProgress());
            }
            mvt::Value value;
            if (it->getFeatureData(false, nullptr)->getVariable("mapbox_clip_start", value)) {
                result.progressIsAField = true;
            }
        }
        return result;
    }
}

void testLineProgress() {
    auto off = makeBuilder(false);
    LineRead plain = readTile(*off, 2, 1, 1);
    TEST_CHECK(plain.progress.size() == 1 && !plain.progress[0], "without line metrics a tile piece carries no progress");

    auto on = makeBuilder(true);
    LineRead west = readTile(*on, 2, 1, 1);
    LineRead east = readTile(*on, 2, 2, 1);
    TEST_CHECK(west.progress.size() == 1 && west.progress[0] && east.progress.size() == 1 && east.progress[0], "with line metrics each cut piece carries its range");
    if (west.progress.size() == 1 && west.progress[0] && east.progress.size() == 1 && east.progress[0]) {
        TEST_CHECK(std::abs(west.progress[0]->start) < 1e-6 && std::abs(east.progress[0]->end - 1) < 1e-6, "the first piece starts the line and the last piece ends it");
        TEST_CHECK(west.progress[0]->end > 0.5 && west.progress[0]->end < 0.55 && east.progress[0]->start < 0.5 && east.progress[0]->start > 0.45,
                   "the two pieces meet at the cut, each reaching a little past it into the tile buffer");
    }

    LineRead whole = readTile(*on, 0, 0, 0);
    TEST_CHECK(whole.progress.size() == 1 && whole.progress[0] && whole.progress[0]->start == 0 && whole.progress[0]->end == 1, "an uncut line is the whole line");

    TEST_CHECK(!west.progressIsAField && !east.progressIsAField && !whole.progressIsAField,
               "the range is not a feature field, so pieces of one line share their feature data");

    on->setLineMetrics(false);
    LineRead after = readTile(*on, 2, 1, 1);
    TEST_CHECK(after.progress.size() == 1 && !after.progress[0], "turning line metrics off drops the cached pieces' ranges");
}
