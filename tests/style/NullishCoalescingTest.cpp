/*
 * `??` is mapbox's coalesce, the form mapbox2css writes it in: a 0 read from a style table is a value.
 * Taken for missing, every Massif badge fell through to the bare glyph's icon halo.
 */

#include "TestCheck.h"

#include <mapnikvt/Expression.h>
#include <mapnikvt/ExpressionUtils.h>
#include <mapnikvt/Feature.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/ParserUtils.h>
#include <mapnikvt/ValueConverter.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mvt = massif::mvt;

namespace {
    float evaluate(const std::string& source, std::vector<std::pair<std::string, mvt::Value>> vars) {
        mvt::ExpressionContext context;
        context.setFeatureData(std::make_shared<mvt::FeatureData>(
            1, mvt::FeatureData::GeometryType::POINT_GEOMETRY, std::move(vars)));
        mvt::Value result = std::visit(mvt::ExpressionEvaluator(context, nullptr), mvt::parseExpression(source, false));
        return mvt::ValueConverter<float>::convert(result);
    }
}

void testNullishCoalescing() {
    TEST_CHECK(evaluate("[r] ?? 1", { { "r", mvt::Value(0LL) } }) == 0.0f, "an integer 0 is a value");
    TEST_CHECK(evaluate("[r] ?? 1", { { "r", mvt::Value(0.0) } }) == 0.0f, "a double 0 is a value");
    TEST_CHECK(evaluate("[r] ?? 1", { { "r", mvt::Value(false) } }) == 0.0f, "false is a value");
    TEST_CHECK(evaluate("[r] ?? 1", {}) == 1.0f, "a missing field falls through");
    TEST_CHECK(evaluate("[r] ?? 1", { { "r", mvt::Value(2.5) } }) == 2.5f, "a value is kept");

    mvt::ExpressionContext context;
    context.setFeatureData(std::make_shared<mvt::FeatureData>(
        1, mvt::FeatureData::GeometryType::POINT_GEOMETRY, std::vector<std::pair<std::string, mvt::Value>> { { "name", mvt::Value(std::string()) }, { "name_int", mvt::Value(std::string("Col")) } }));
    mvt::Value name = std::visit(mvt::ExpressionEvaluator(context, nullptr), mvt::parseExpression("[name] ?? [name_int]", false));
    TEST_CHECK(mvt::ValueConverter<std::string>::convert(name) == "Col", "an empty string still falls through");
}
