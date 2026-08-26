/*
 * resolveLayerConfig over a style layer that carries ORDINARY styling rules, which is what a
 * converted MapBox style produces for '#contour' / '#hillshade' (see tools/style-cli). The
 * resolver runs with no tile and no feature, so a feature filter on such a rule has nothing to
 * read - evaluating it used to dereference a null FeatureData.
 */

#include "TestCheck.h"

#include "mapnikvt/LayerConfigResolver.h"
#include "mapnikvt/ContourConfigSymbolizer.h"
#include "mapnikvt/ExpressionContext.h"
#include "mapnikvt/Filter.h"
#include "mapnikvt/Layer.h"
#include "mapnikvt/Map.h"
#include "mapnikvt/Predicate.h"
#include "mapnikvt/Rule.h"
#include "mapnikvt/Style.h"
#include "mapnikvt/Symbolizer.h"

#include <memory>
#include <string>
#include <vector>

using namespace massif::mvt;

namespace {
    class NullLogger : public Logger {
    public:
        void write(Severity, const std::string&) override { }
    };

    // Stands in for a line/text symbolizer: those pull the vt tile builders in, and all this
    // suite needs is a symbolizer that is NOT a LayerConfigSymbolizer.
    class TestGeometrySymbolizer : public Symbolizer {
    public:
        explicit TestGeometrySymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) { }

        FeatureProcessor createFeatureProcessor(const ExpressionContext&, const SymbolizerContext&) const override {
            return FeatureProcessor();
        }
    };

    // '[mapnik::geometry_type] = 2', i.e. what '[$type] = "line"' converts to.
    std::shared_ptr<const Filter> geometryTypeFilter() {
        Predicate pred = std::make_shared<ComparisonPredicate>(ComparisonPredicate::Op::EQ,
            Expression(std::make_shared<VariableExpression>(std::string("mapnik::geometry_type"))),
            Expression(Value(static_cast<long long>(2))));
        return std::make_shared<Filter>(Filter::Type::FILTER, std::optional<Predicate>(pred));
    }

    std::shared_ptr<Style> makeStyle(const std::string& name, std::vector<std::shared_ptr<const Rule>> rules) {
        return std::make_shared<Style>(name, 1.0f, "", std::optional<massif::vt::CompOp>(), Style::FilterMode::ALL, "", std::move(rules));
    }

    void addLayer(Map& map, const std::shared_ptr<Style>& style) {
        map.addStyle(style);
        map.addLayer(std::make_shared<Layer>(style->getName(), std::vector<std::string> { style->getName() }));
    }
}

void testLayerConfig() {
    auto logger = std::make_shared<NullLogger>();

    // A '#contour' layer holding only line rules: no config symbolizer, so no config - and the
    // feature filter must not be evaluated on the way there.
    {
        Map map { Map::Settings() };
        auto rule = std::make_shared<Rule>("line", 0, 24, geometryTypeFilter(),
            std::vector<std::shared_ptr<const Symbolizer>> { std::make_shared<TestGeometrySymbolizer>(logger) });
        addLayer(map, makeStyle("contour", { rule }));

        ResolvedLayerConfig config = resolveLayerConfig(map, "contour", 12.0f, nullptr);
        TEST_CHECK(!config.visible, "a styling-only layer resolves to no config");
        TEST_CHECK(config.values.empty(), "a styling-only layer resolves no values");
    }

    // The same layer with a config rule beside the line rules: the config still resolves.
    {
        Map map { Map::Settings() };
        auto lineRule = std::make_shared<Rule>("line", 0, 24, geometryTypeFilter(),
            std::vector<std::shared_ptr<const Symbolizer>> { std::make_shared<TestGeometrySymbolizer>(logger) });
        auto configSymbolizer = std::make_shared<ContourConfigSymbolizer>(logger);
        configSymbolizer->getProperty("base-interval")->setExpression(Expression(Value(50.0)));
        auto configRule = std::make_shared<Rule>("config", 0, 24, std::shared_ptr<const Filter>(),
            std::vector<std::shared_ptr<const Symbolizer>> { configSymbolizer });
        addLayer(map, makeStyle("contour", { lineRule, configRule }));

        ResolvedLayerConfig config = resolveLayerConfig(map, "contour", 12.0f, nullptr);
        TEST_CHECK(config.visible, "a config rule beside styling rules is still found");
        auto it = config.values.find("base-interval");
        TEST_CHECK(it != config.values.end() && ValueConverter<float>::convert(it->second) == 50.0f,
                   "the config rule's value is resolved");
    }

    // The zoom range ignores styling rules the same way.
    {
        Map map { Map::Settings() };
        auto rule = std::make_shared<Rule>("line", 5, 15, geometryTypeFilter(),
            std::vector<std::shared_ptr<const Symbolizer>> { std::make_shared<TestGeometrySymbolizer>(logger) });
        addLayer(map, makeStyle("contour", { rule }));

        std::pair<int, int> range = resolveLayerZoomRange(map, "contour");
        TEST_CHECK(range.first == 0 && range.second == 24, "a styling-only layer is zoom-unconstrained");
    }

    // A mapnik:: variable with no feature under it is undefined, not a crash - the same
    // expression reached from anywhere else out of a tile.
    {
        ExpressionContext exprContext;
        TEST_CHECK(std::holds_alternative<std::monostate>(exprContext.getVariable("mapnik::geometry_type")),
                   "mapnik::geometry_type is null without a feature");
        TEST_CHECK(std::holds_alternative<std::monostate>(exprContext.getVariable("mapnik::feature_id")),
                   "mapnik::feature_id is null without a feature");
    }
}
