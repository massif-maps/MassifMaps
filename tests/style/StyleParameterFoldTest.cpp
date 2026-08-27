/*
 * When a style parameter is resolved: while the tile is decoded, or per frame from the store. A
 * parameter read together with a feature field cannot be live (isLiveCapable), so it has to fold
 * at decode - otherwise every feature keeps its own closure over the store, which re-runs the
 * expression interpreter per frame and splits the batches. See docs/features/style-parameters.md.
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
    mvt::ExpressionContext contextWith(std::vector<std::pair<std::string, mvt::Value>> vars, const std::shared_ptr<const mvt::StyleParameterStore>& store) {
        mvt::ExpressionContext context;
        context.setFeatureData(std::make_shared<mvt::FeatureData>(
            1, mvt::FeatureData::GeometryType::POINT_GEOMETRY, std::move(vars)));
        context.setStyleParameterStore(store);
        return context;
    }

    mvt::Value makeTable(std::map<std::string, mvt::Value> members) {
        return mvt::Value(std::make_shared<const mvt::ValueObject>(std::move(members)));
    }
}

void testStyleParameterFold() {
    // 1. A table parameter indexed by a field - the app-owned POI ranking - folds to a constant,
    // so the culler and the renderer see a plain value and features answering alike share it.
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

    // 2. A parameter-only property must NOT fold: it is live, and the tiles read it through the
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
