/*
 * Symbolizer::isPropertyDefined, which TileReader asks of every style a layer filter rejects: a
 * style whose symbolizer SETS elevation-mode can lift a deck, and is built anyway so the span
 * chords reach every CompositeVectorTileLayer group.
 *
 * Not covered: TileReader::readTile itself, which links vt's tile builders and freetype (see
 * ../README.md). That the filtered decode draws the same map is a device check.
 */

#include "TestCheck.h"

#include <mapnikvt/Properties.h>
#include <mapnikvt/Symbolizer.h>

#include <memory>
#include <string>

namespace mvt = massif::mvt;

namespace {
    struct NullLogger : mvt::Logger {
        void write(Severity, const std::string&) override { }
    };

    struct SpanSymbolizer : mvt::Symbolizer {
        SpanSymbolizer() : Symbolizer(std::make_shared<NullLogger>()) {
            bindProperty("elevation-mode", &_elevationMode);
        }

        FeatureProcessor createFeatureProcessor(const mvt::ExpressionContext&, const mvt::SymbolizerContext&) const override {
            return FeatureProcessor();
        }

        mvt::StringProperty _elevationMode = mvt::StringProperty("drape");
    };
}

void testSymbolizerProperty() {
    SpanSymbolizer symbolizer;
    TEST_CHECK(!symbolizer.isPropertyDefined("elevation-mode"), "a bound property left at its default is not defined");

    symbolizer.getProperty("elevation-mode")->setExpression(mvt::Value(std::string("span")));
    TEST_CHECK(symbolizer.isPropertyDefined("elevation-mode"), "a property the style sets is defined");

    bool threw = false;
    try {
        TEST_CHECK(!symbolizer.isPropertyDefined("line-width"), "a property the symbolizer does not bind is not defined");
    }
    catch (const std::exception&) {
        threw = true;
    }
    TEST_CHECK(!threw, "and asking for it does not throw, unlike getProperty");
}
