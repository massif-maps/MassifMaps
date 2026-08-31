/*
 * Whether a symbolizer property VALUE can read a feature field. docs/contributing/style-tools.md
 * says it cannot ("createFeatureProcessor runs once per rule, with no feature bound") and mapbox2css
 * splits every field-driven paint value into one attachment per branch because of it. The code says
 * otherwise - TileReader binds the feature data before it builds the processor - so these pin what
 * the property layer actually does, and what the failure it was blamed for really is.
 */

#include "TestCheck.h"

#include <mapnikvt/Feature.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/ParserUtils.h>
#include <mapnikvt/Properties.h>
#include <mapnikvt/StyleParameterStore.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mvt = massif::mvt;
namespace vt = massif::vt;

namespace {
    // The context TileReader hands a symbolizer: the feature's data, narrowed to the fields the
    // rule references (TileReader::processLayer -> exprContext.setFeatureData).
    mvt::ExpressionContext contextWith(std::vector<std::pair<std::string, mvt::Value>> vars, std::shared_ptr<const mvt::StyleParameterStore> store = std::shared_ptr<const mvt::StyleParameterStore>()) {
        mvt::ExpressionContext context;
        context.setFeatureData(std::make_shared<mvt::FeatureData>(
            1, mvt::FeatureData::GeometryType::LINE_GEOMETRY, std::move(vars)));
        context.setStyleParameterStore(std::move(store));
        return context;
    }

    mvt::Value makeTable(std::map<std::string, mvt::Value> members) {
        return mvt::Value(std::make_shared<const mvt::ValueObject>(std::move(members)));
    }

    vt::Color colorOf(const mvt::ColorFunctionProperty& prop, const mvt::ExpressionContext& context) {
        return prop.getFunction(context)(vt::ViewState());
    }

    const vt::Color RED = vt::Color::fromValue(0xffff0000);
    const vt::Color BLUE = vt::Color::fromValue(0xff0000ff);
}

void testDataDrivenProperty() {
    // 1. A field in a colour VALUE reaches the feature, and gives a different colour per feature.
    // This is the claim the converter's attachment split exists to work around.
    {
        mvt::ColorFunctionProperty stroke("#000000");
        stroke.setExpression(mvt::parseExpression("[class] = 'motorway' ? '#ff0000' : '#0000ff'", false));

        vt::Color motorway = colorOf(stroke, contextWith({ { "class", mvt::Value(std::string("motorway")) } }));
        vt::Color track = colorOf(stroke, contextWith({ { "class", mvt::Value(std::string("track")) } }));

        TEST_CHECK(motorway == RED, "a field in a colour value is evaluated against the bound feature");
        TEST_CHECK(track == BLUE, "and a second feature gets its own colour, not a cached one");
    }

    // 2. Reading a field must NOT make the property live: a live property is evaluated per frame
    // from the parameter store, and a feature field is not in it. isLiveCapable gates the
    // no-re-decode path in MBVectorTileDecoder, so a wrong answer here paints stale tiles.
    {
        mvt::ColorFunctionProperty stroke("#000000");
        stroke.setExpression(mvt::parseExpression("[class] = 'motorway' ? '#ff0000' : '#0000ff'", false));
        TEST_CHECK(!stroke.isLiveCapable(), "a field-reading property is never live");

        mvt::ColorFunctionProperty param("#000000");
        param.setExpression(mvt::parseExpression("[param::road_color]", false));
        TEST_CHECK(param.isLiveCapable(), "a parameter-only property still is");
    }

    // 3. A feature MISSING the field yields an unset value. It used to reach parseColor("") and
    // throw - the "Color parsing failed" measured on topo-v4 - and TileReader caches a NULL
    // processor when it does, so the geometry went with the colour. It now falls back to the
    // property's declared default.
    {
        mvt::ColorFunctionProperty stroke("#0000ff");
        stroke.setExpression(mvt::parseExpression("[color]", false));

        vt::Color missing = colorOf(stroke, contextWith({ { "class", mvt::Value(std::string("track")) } }));
        TEST_CHECK(missing == BLUE, "a bare field colour falls back to the default when the field is absent");

        vt::Color present = colorOf(stroke, contextWith({ { "color", mvt::Value(std::string("#ff0000")) } }));
        TEST_CHECK(present == RED, "and resolves normally when it is there");
    }

    // 4. A malformed value is NOT an unset one and still throws: that is a style bug, and silently
    // painting the default over it would hide it.
    {
        mvt::ColorFunctionProperty stroke("#0000ff");
        stroke.setExpression(mvt::parseExpression("[color]", false));

        bool threw = false;
        try {
            colorOf(stroke, contextWith({ { "color", mvt::Value(std::string("not-a-colour")) } }));
        } catch (const mvt::ParserException&) {
            threw = true;
        }
        TEST_CHECK(threw, "a malformed colour still throws");
    }

    // 5. The float half of the same bug: it took the missing field to 0 silently, which drops the
    // line just as effectively and is harder to see.
    {
        mvt::FloatFunctionProperty width(1.0f);
        width.setExpression(mvt::parseExpression("[width]", false));
        float missing = width.getFunction(contextWith({ { "class", mvt::Value(std::string("track")) } }))(vt::ViewState());
        TEST_CHECK(missing == 1.0f, "a bare field width falls back to the default, not 0");

        float present = width.getFunction(contextWith({ { "width", mvt::Value(3.0) } }))(vt::ViewState());
        TEST_CHECK(present == 3.0f, "and takes the field when it is there");
    }

    // 6. An explicit guard in the style keeps working, and still wins over the default.
    {
        mvt::ColorFunctionProperty stroke("#000000");
        stroke.setExpression(mvt::parseExpression("[color] <> null ? [color] : '#0000ff'", false));
        vt::Color missing = colorOf(stroke, contextWith({ { "class", mvt::Value(std::string("track")) } }));
        TEST_CHECK(missing == BLUE, "a null-guarded field colour takes the guard's branch");
    }

    // 6. What makes it CHEAP, and what a data-driven IR has to keep: a field-driven value folds to a
    // constant per feature, so two features answering alike hand back EQUAL functions.
    // TileLayerBuilder dedups on exactly that equality into one of the geometry's 16 style slots
    // (TileLayerBuilder.cpp:264), so a field-driven colour costs slots, not batches.
    {
        mvt::ColorFunctionProperty stroke("#000000");
        stroke.setExpression(mvt::parseExpression("[class] = 'motorway' ? '#ff0000' : '#0000ff'", false));

        vt::ColorFunction a = stroke.getFunction(contextWith({ { "class", mvt::Value(std::string("motorway")) } }));
        vt::ColorFunction b = stroke.getFunction(contextWith({ { "class", mvt::Value(std::string("motorway")) } }));
        vt::ColorFunction c = stroke.getFunction(contextWith({ { "class", mvt::Value(std::string("track")) } }));

        TEST_CHECK(a.function() == nullptr, "a field-driven colour folds to a constant, not a per-frame function");
        TEST_CHECK(a == b, "two features with the same field value share one style slot");
        TEST_CHECK(a != c, "two features with different field values do not");
    }

    // 7. A table parameter indexed by a field - get([param::ranks], [class]) - folds the same way.
    // It is not live-capable (the field is not in the store), so a change decodes the tiles again;
    // keeping the store behind a closure would only cost an interpreter run per feature per frame
    // and a function object per feature, which splits the batches.
    {
        auto store = std::make_shared<mvt::StyleParameterStore>();
        store->setValues({ { "ranks", makeTable({ { "peak", mvt::Value(900.0) } }) } });

        mvt::FloatFunctionProperty rank(0.0f);
        rank.setExpression(mvt::parseExpression("get([param::ranks], [class], 100)", false));
        TEST_CHECK(!rank.isLiveCapable(), "a table read by a field is not a live parameter");

        vt::FloatFunction peak = rank.getFunction(contextWith({ { "class", mvt::Value(std::string("peak")) } }, store));
        vt::FloatFunction peak2 = rank.getFunction(contextWith({ { "class", mvt::Value(std::string("peak")) } }, store));
        vt::FloatFunction zoo = rank.getFunction(contextWith({ { "class", mvt::Value(std::string("zoo")) } }, store));

        TEST_CHECK(peak.function() == nullptr, "a table read by a field folds to a constant");
        TEST_CHECK(peak(vt::ViewState()) == 900.0f, "and it is the table's value for that feature");
        TEST_CHECK(zoo(vt::ViewState()) == 100.0f, "a key the table misses takes the fallback");
        TEST_CHECK(peak == peak2, "two features answering alike share one function object");
    }

    // 8. A parameter-only property must NOT fold: it is live, and the tiles read it through the
    // store, so setting the parameter repaints instead of decoding.
    {
        auto store = std::make_shared<mvt::StyleParameterStore>();
        store->setValues({ { "boost", mvt::Value(10.0) } });

        mvt::FloatFunctionProperty rank(0.0f);
        rank.setExpression(mvt::parseExpression("[param::boost]", false));
        TEST_CHECK(rank.isLiveCapable(), "a parameter-only property is live");

        vt::FloatFunction func = rank.getFunction(contextWith({ }, store));
        TEST_CHECK(func.function() != nullptr, "so it stays a function reading the store");
        TEST_CHECK(func(vt::ViewState()) == 10.0f, "which gives the current value");

        store->setValues({ { "boost", mvt::Value(20.0) } });
        TEST_CHECK(func(vt::ViewState()) == 20.0f, "and follows a change with no new function");
    }
}
