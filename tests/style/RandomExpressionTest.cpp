/*
 * random(min, max[, seed]) is mapbox's: one deterministic draw per seed, the feature id by default, or
 * the placed instance where a symbolizer places one per point of a MultiPoint.
 */

#include "TestCheck.h"

#include <mapnikvt/Expression.h>
#include <mapnikvt/ExpressionUtils.h>
#include <mapnikvt/Feature.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/ParserUtils.h>
#include <mapnikvt/ValueConverter.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mvt = massif::mvt;

namespace {
    double evaluate(const std::string& source, long long featureId, std::optional<long long> instanceSeed = std::nullopt) {
        mvt::ExpressionContext context;
        context.setFeatureData(std::make_shared<mvt::FeatureData>(featureId, mvt::FeatureData::GeometryType::POINT_GEOMETRY, std::vector<std::pair<std::string, mvt::Value>> { { "osmid", mvt::Value(42LL) } }));
        context.setInstanceSeed(instanceSeed);
        return mvt::ValueConverter<double>::convert(std::visit(mvt::ExpressionEvaluator(context, nullptr), mvt::parseExpression(source, false)));
    }
}

void testRandomExpression() {
    TEST_CHECK(evaluate("random(0, 10, 5)", 1) == evaluate("random(0, 10, 5)", 2), "an explicit seed gives the same value on every feature");
    TEST_CHECK(evaluate("random(0, 10, [osmid])", 1) == evaluate("random(0, 10, 42)", 3), "a field can be the seed");
    TEST_CHECK(evaluate("random(0, 10)", 7) == evaluate("random(0, 10, 7)", 1), "without a seed, the feature id seeds it");
    TEST_CHECK(evaluate("random(0, 10)", 7) != evaluate("random(0, 10)", 8), "two features draw different values");
    TEST_CHECK(evaluate("random(0, 10)", 7, 1234) == evaluate("random(0, 10, 1234)", 7), "an instance seed overrides the feature id");
    TEST_CHECK(evaluate("random(0, 10, 'oak')", 1) == evaluate("random(0, 10, 'oak')", 9), "a string seed is hashed the same way every time");

    double sum = 0, lo = 1e9, hi = -1e9;
    for (long long id = 1; id <= 2000; id++) {
        double v = evaluate("random(2, 4)", id);
        sum += v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    TEST_CHECK(lo >= 2 && hi < 4 && hi - lo > 1.9, "draws stay in [min, max) and cover it");
    TEST_CHECK(sum / 2000 > 2.9 && sum / 2000 < 3.1, "draws are spread evenly");

    // How a model rule picks one of three files per point: a step over strings keyed by a draw
    std::set<std::string> files;
    mvt::Expression pick = mvt::parseExpression("step(random(0, 3), 0, 'a.glb', 1, 'b.glb', 2, 'c.glb')", false);
    for (long long seed = 1; seed <= 60; seed++) {
        mvt::ExpressionContext context;
        context.setFeatureData(std::make_shared<mvt::FeatureData>(1, mvt::FeatureData::GeometryType::POINT_GEOMETRY, std::vector<std::pair<std::string, mvt::Value>>()));
        context.setInstanceSeed(seed);
        files.insert(mvt::ValueConverter<std::string>::convert(std::visit(mvt::ExpressionEvaluator(context, nullptr), pick)));
    }
    TEST_CHECK(files == std::set<std::string>({ "a.glb", "b.glb", "c.glb" }), "a step over random(0, 3) picks each of three files");
}
