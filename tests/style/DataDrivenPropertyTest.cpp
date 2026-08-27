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

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mvt = massif::mvt;
namespace vt = massif::vt;

namespace {
    // The context TileReader hands a symbolizer: the feature's data, narrowed to the fields the
    // rule references (TileReader::processLayer -> exprContext.setFeatureData).
    mvt::ExpressionContext contextWith(std::vector<std::pair<std::string, mvt::Value>> vars) {
        mvt::ExpressionContext context;
        context.setFeatureData(std::make_shared<mvt::FeatureData>(
            1, mvt::FeatureData::GeometryType::LINE_GEOMETRY, std::move(vars)));
        return context;
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
}
