/*
 * resolveLegendEntry / resolveLegend over a hand-built map: rule matching (filters, else-filter,
 * zoom range), draw order across layers sharing a source layer, evaluated colours and widths, a
 * style parameter change reaching the result, and the swatch each item resolves to. Not covered: a
 * real CartoCSS compile, whose symbolizers pull vt and freetype in (see ../README.md) -
 * tools/style-cli/test/legend.test.js runs that path through `massif-style legend`.
 */

#include "TestCheck.h"

#include <mapnikvt/LegendResolver.h>
#include <mapnikvt/Filter.h>
#include <mapnikvt/Layer.h>
#include <mapnikvt/Map.h>
#include <mapnikvt/ParserUtils.h>
#include <mapnikvt/Predicate.h>
#include <mapnikvt/Properties.h>
#include <mapnikvt/Rule.h>
#include <mapnikvt/Style.h>
#include <mapnikvt/StyleParameterStore.h>
#include <mapnikvt/Symbolizer.h>

#include <rapidjson/document.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace massif::mvt;

namespace {
    class NullLogger : public Logger {
    public:
        void write(Severity, const std::string&) override { }
    };

    class TestLineSymbolizer : public Symbolizer {
    public:
        explicit TestLineSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            bindProperty("stroke", &_stroke);
            bindProperty("stroke-width", &_strokeWidth);
            bindProperty("stroke-opacity", &_strokeOpacity);
            bindProperty("stroke-dasharray", &_strokeDashArray);
        }

        FeatureProcessor createFeatureProcessor(const ExpressionContext&, const SymbolizerContext&) const override { return FeatureProcessor(); }
        std::string getTypeName() const override { return "line"; }

        TestLineSymbolizer& set(const std::string& name, const Expression& expr) {
            getProperty(name)->setExpression(expr);
            return *this;
        }

    private:
        ColorFunctionProperty _stroke = ColorFunctionProperty("#000000");
        FloatFunctionProperty _strokeWidth = FloatFunctionProperty(1.0f);
        FloatFunctionProperty _strokeOpacity = FloatFunctionProperty(1.0f);
        StringProperty _strokeDashArray = StringProperty("");
    };

    class TestPolygonSymbolizer : public Symbolizer {
    public:
        explicit TestPolygonSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            bindProperty("fill", &_fill);
            bindProperty("fill-opacity", &_fillOpacity);
        }

        FeatureProcessor createFeatureProcessor(const ExpressionContext&, const SymbolizerContext&) const override { return FeatureProcessor(); }
        std::string getTypeName() const override { return "polygon"; }

        ColorFunctionProperty _fill = ColorFunctionProperty("#808080");
        FloatFunctionProperty _fillOpacity = FloatFunctionProperty(1.0f);
    };

    class TestTextSymbolizer : public Symbolizer {
    public:
        explicit TestTextSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            bindProperty("name", &_name);
            bindProperty("fill", &_fill);
            bindProperty("background-fill", &_backgroundFill);
        }

        FeatureProcessor createFeatureProcessor(const ExpressionContext&, const SymbolizerContext&) const override { return FeatureProcessor(); }
        std::string getTypeName() const override { return "text"; }

        StringProperty _name = StringProperty("");
        ColorFunctionProperty _fill = ColorFunctionProperty("#000000");
        ColorFunctionProperty _backgroundFill = ColorFunctionProperty("transparent");
    };

    // A layer config symbolizer, say: it draws no feature, so a legend skips it.
    class TestConfigSymbolizer : public Symbolizer {
    public:
        explicit TestConfigSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) { }

        FeatureProcessor createFeatureProcessor(const ExpressionContext&, const SymbolizerContext&) const override { return FeatureProcessor(); }
    };

    std::shared_ptr<const Filter> fieldFilter(const std::string& field, const Value& value) {
        Predicate pred = std::make_shared<ComparisonPredicate>(ComparisonPredicate::Op::EQ,
            Expression(std::make_shared<VariableExpression>(field)), Expression(value));
        return std::make_shared<Filter>(Filter::Type::FILTER, std::optional<Predicate>(pred));
    }

    void addLayer(Map& map, const std::string& layerName, const std::shared_ptr<Style>& style) {
        map.addStyle(style);
        map.addLayer(std::make_shared<Layer>(layerName, std::vector<std::string> { style->getName() }));
    }

    // '#transportation::casing' under every path, then '#transportation' by SAC grade with an
    // else-rule, then '#water' at half opacity - two layers reading one source layer.
    std::shared_ptr<Map> buildMap(const std::shared_ptr<Logger>& logger) {
        auto map = std::make_shared<Map>(Map::Settings());

        auto casing = std::make_shared<TestLineSymbolizer>(logger);
        casing->set("stroke", Value(std::string("#ffffff"))).set("stroke-width", Value(4.0));
        addLayer(*map, "transportation", std::make_shared<Style>("transportation::casing", 1.0f, "", std::optional<massif::vt::CompOp>(), Style::FilterMode::FIRST, "", std::vector<std::shared_ptr<const Rule>> {
            std::make_shared<Rule>("casing", 14, 25, fieldFilter("class", Value(std::string("path"))), std::vector<std::shared_ptr<const Symbolizer>> { casing })
        }));

        auto sac1 = std::make_shared<TestLineSymbolizer>(logger);
        sac1->set("stroke", parseExpression("[param::trail_color]", false)).set("stroke-width", parseExpression("[view::zoom] - 13", false)).set("stroke-dasharray", Value(std::string("4,2")));
        auto other = std::make_shared<TestLineSymbolizer>(logger);
        other->set("stroke", Value(std::string("#808080")));
        addLayer(*map, "transportation", std::make_shared<Style>("transportation", 1.0f, "", std::optional<massif::vt::CompOp>(), Style::FilterMode::FIRST, "", std::vector<std::shared_ptr<const Rule>> {
            std::make_shared<Rule>("sac1", 14, 25, fieldFilter("sac_scale", Value(1LL)), std::vector<std::shared_ptr<const Symbolizer>> { sac1, std::make_shared<TestConfigSymbolizer>(logger) }),
            std::make_shared<Rule>("other", 14, 25, std::make_shared<Filter>(Filter::Type::ELSEFILTER, std::optional<Predicate>()), std::vector<std::shared_ptr<const Symbolizer>> { other })
        }));

        auto water = std::make_shared<TestPolygonSymbolizer>(logger);
        water->_fill.setExpression(Value(std::string("rgba(0, 0, 255, 0.5)")));
        addLayer(*map, "water", std::make_shared<Style>("water", 0.5f, "", std::optional<massif::vt::CompOp>(), Style::FilterMode::FIRST, "", std::vector<std::shared_ptr<const Rule>> {
            std::make_shared<Rule>("water", 0, 25, std::shared_ptr<const Filter>(), std::vector<std::shared_ptr<const Symbolizer>> { water })
        }));
        auto plate = std::make_shared<TestTextSymbolizer>(logger);
        plate->_name.setExpression(parseExpression("[ref]", false));
        plate->_fill.setExpression(Value(std::string("#ffffff")));
        plate->_backgroundFill.setExpression(Value(std::string("#c0392b")));
        addLayer(*map, "transportation_name", std::make_shared<Style>("transportation_name::shield", 1.0f, "", std::optional<massif::vt::CompOp>(), Style::FilterMode::FIRST, "", std::vector<std::shared_ptr<const Rule>> {
            std::make_shared<Rule>("shield", 0, 25, std::shared_ptr<const Filter>(), std::vector<std::shared_ptr<const Symbolizer>> { plate })
        }));
        return map;
    }

    std::string stringValue(const LegendSymbolizer& symbolizer, const std::string& name) {
        auto it = symbolizer.values.find(name);
        return it != symbolizer.values.end() ? ValueConverter<std::string>::convert(it->second) : std::string("<unset>");
    }

    float floatValue(const LegendSymbolizer& symbolizer, const std::string& name) {
        auto it = symbolizer.values.find(name);
        return it != symbolizer.values.end() ? ValueConverter<float>::convert(it->second) : -1.0f;
    }
}

void testLegendResolver() {
    auto logger = std::make_shared<NullLogger>();
    std::shared_ptr<Map> map = buildMap(logger);
    auto store = std::make_shared<StyleParameterStore>(std::map<std::string, Value> { { "trail_color", Value(std::string("#c0392b")) } });
    std::vector<std::pair<std::string, Value>> sac1Path { { "class", Value(std::string("path")) }, { "sac_scale", Value(1LL) } };

    // A T1 path: the casing layer, then its grade rule; the config symbolizer beside it is left out.
    {
        std::vector<LegendSymbolizer> symbolizers = resolveLegendEntry(*map, "transportation", FeatureData::GeometryType::LINE_GEOMETRY, sac1Path, 15.0f, store);
        TEST_CHECK(symbolizers.size() == 2, "a T1 path draws the casing and its grade line, nothing else");
        if (symbolizers.size() == 2) {
            TEST_CHECK(symbolizers[0].style == "transportation::casing" && symbolizers[1].style == "transportation", "in the order the layers draw");
            TEST_CHECK(symbolizers[0].type == "line", "named by the symbolizer's type");
            TEST_CHECK(stringValue(symbolizers[0], "stroke") == "#ffffff" && floatValue(symbolizers[0], "stroke-width") == 4.0f, "the casing's own colour and width");
            TEST_CHECK(stringValue(symbolizers[1], "stroke") == "#c0392b", "a parameter colour reads the store");
            TEST_CHECK(floatValue(symbolizers[1], "stroke-width") == 2.0f, "a view::zoom width is evaluated at the entry's zoom");
            TEST_CHECK(stringValue(symbolizers[1], "stroke-dasharray") == "4,2", "a set string property is there");
            TEST_CHECK(floatValue(symbolizers[1], "stroke-opacity") == 1.0f, "a key property the style left unset is there at its default");
            TEST_CHECK(symbolizers[1].values.count("comp-op") == 0, "a non-key property the style left unset is not");
        }
    }

    // The same entry once the app recolours the trails.
    {
        auto recoloured = std::make_shared<StyleParameterStore>(std::map<std::string, Value> { { "trail_color", Value(std::string("#00ff00")) } });
        std::vector<LegendSymbolizer> symbolizers = resolveLegendEntry(*map, "transportation", FeatureData::GeometryType::LINE_GEOMETRY, sac1Path, 15.0f, recoloured);
        TEST_CHECK(symbolizers.size() == 2 && stringValue(symbolizers[1], "stroke") == "#00ff00", "changing the parameter changes the legend colour");
    }

    // A T3 path falls to the else-rule, whose unset width is the default.
    {
        std::vector<std::pair<std::string, Value>> sac3Path { { "class", Value(std::string("path")) }, { "sac_scale", Value(3LL) } };
        std::vector<LegendSymbolizer> symbolizers = resolveLegendEntry(*map, "transportation", FeatureData::GeometryType::LINE_GEOMETRY, sac3Path, 15.0f, store);
        TEST_CHECK(symbolizers.size() == 2 && stringValue(symbolizers[1], "stroke") == "#808080", "an unmatched grade takes the else-rule");
        TEST_CHECK(symbolizers.size() == 2 && floatValue(symbolizers[1], "stroke-width") == 1.0f, "at the default width");
    }

    TEST_CHECK(resolveLegendEntry(*map, "transportation", FeatureData::GeometryType::LINE_GEOMETRY, sac1Path, 12.0f, store).empty(), "below every rule's zoom nothing draws the entry");
    TEST_CHECK(resolveLegendEntry(*map, "roads", FeatureData::GeometryType::LINE_GEOMETRY, sac1Path, 15.0f, store).empty(), "a source layer the style has no layer for draws nothing");

    {
        std::vector<LegendSymbolizer> symbolizers = resolveLegendEntry(*map, "water", FeatureData::GeometryType::POLYGON_GEOMETRY, {}, 10.0f, store);
        TEST_CHECK(symbolizers.size() == 1 && stringValue(symbolizers[0], "fill") == "#0000ff80", "a translucent colour keeps its alpha");
        TEST_CHECK(symbolizers.size() == 1 && symbolizers[0].styleOpacity == 0.5f, "and the style's own opacity comes along");
    }

    // The JSON form: one swatch per item, in the spec's sections, labels untouched, zoom inherited.
    {
        std::string spec = R"({"zoom": 15, "sections": [{"id": "trails", "label": {"en": "Trails", "fr": "Sentiers"}, "items": [
            {"id": "t1", "label": "T1", "layer": "transportation", "geometry": "line", "properties": {"class": "path", "sac_scale": 1}},
            {"id": "t1-casing", "layer": "transportation", "geometry": "line", "attachment": "^casing$", "properties": {"class": "path", "sac_scale": 1}},
            {"id": "lake", "label": "Lake", "layer": "water", "geometry": "polygon", "zoom": 8},
            {"id": "a7", "label": "Motorway number", "layer": "transportation_name", "geometry": "line", "properties": {"ref": "A 7"}}]},
            {"id": "hidden", "items": [{"id": "road", "layer": "roads", "geometry": "line"}]}]})";
        rapidjson::Document doc;
        doc.Parse(resolveLegend(*map, spec, store).c_str());
        TEST_CHECK(!doc.HasParseError(), "the result is JSON");
        if (!doc.HasParseError()) {
            TEST_CHECK(doc["sections"].Size() == 1, "a section nothing in it draws is dropped");
            const rapidjson::Value& section = doc["sections"][0];
            TEST_CHECK(std::string(section["label"]["fr"].GetString()) == "Sentiers", "a localised label passes through");
            const rapidjson::Value& items = section["items"];
            TEST_CHECK(items.Size() == 4, "every drawn item is there");
            if (items.Size() == 4) {
                const rapidjson::Value& t1 = items[0];
                TEST_CHECK(std::string(t1["kind"].GetString()) == "line" && !t1.HasMember("properties") && !t1.HasMember("layer"), "a line item, without its feature");
                TEST_CHECK(t1["lines"].Size() == 2 && std::string(t1["lines"][0]["color"].GetString()) == "#ffffff", "its lines bottom to top, the casing first");
                TEST_CHECK(t1["lines"].Size() == 2 && std::string(t1["lines"][1]["color"].GetString()) == "#c0392b" && t1["lines"][1]["width"].GetDouble() == 2.0, "then the grade line, at the spec's zoom");
                TEST_CHECK(t1["lines"].Size() == 2 && t1["lines"][1]["dasharray"].Size() == 2 && t1["lines"][1]["dasharray"][0].GetDouble() == 4.0, "its dashes as numbers");
                TEST_CHECK(items[1]["lines"].Size() == 1, "an attachment pattern keeps only the matching attachments");
                const rapidjson::Value& lake = items[2];
                TEST_CHECK(std::string(lake["kind"].GetString()) == "fill" && std::string(lake["color"].GetString()) == "#0000ff80", "a fill item with its colour");
                TEST_CHECK(lake["opacity"].GetDouble() == 0.5, "and the style opacity");
                const rapidjson::Value& a7 = items[3];
                TEST_CHECK(std::string(a7["kind"].GetString()) == "shield", "a text on a plate is a road number");
                TEST_CHECK(std::string(a7["text"]["value"].GetString()) == "A 7" && std::string(a7["plate"]["color"].GetString()) == "#c0392b", "with its text and plate");
            }
        }

        bool threw = false;
        try {
            resolveLegend(*map, R"({"zoom": 15, "sections": [{"items": [{"layer": "water"}]}]})", store);
        }
        catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST_CHECK(threw, "an item without a geometry is rejected");
    }
}
