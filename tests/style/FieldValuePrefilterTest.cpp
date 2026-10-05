// A converted style is one attachment per feature class, and each attachment used to walk its whole
// source layer per tile. TileReader now drops a rule whose equality on a field names a value the
// tile layer does not carry (PredicateFieldValueEvaluator over MBVTFeatureDecoder::mayHaveFieldValue),
// and the decoder keeps one feature-data cache per field set instead of a single slot.
// Not covered: TileReader::readTile itself (it links vt's builders); the decode it saves is measured
// with libs-massif/cartocss/util/bench-decode, which also checks the tiles come out identical.

#include "TestCheck.h"

#include <mapnikvt/Expression.h>
#include <mapnikvt/Logger.h>
#include <mapnikvt/MBVTFeatureDecoder.h>
#include <mapnikvt/MBVTSubtile.h>
#include <mapnikvt/PredicateUtils.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace mvt = massif::mvt;
namespace detail = massif::mvt::subtile_detail;

namespace {
    struct NullLogger final : mvt::Logger {
        void write(Severity, const std::string&) override { }
    };

    std::string packed(const std::vector<std::uint64_t>& values) {
        std::string out;
        for (std::uint64_t value : values) {
            detail::writeVarint(out, value);
        }
        return out;
    }

    std::string stringValue(const std::string& str) {
        std::string value;
        detail::writeBytes(value, 1, str);
        return value;
    }

    std::string intValue(int i) {
        std::string value;
        detail::writeVarint(value, 4 << 3);
        detail::writeVarint(value, i);
        return value;
    }

    std::string pointFeature(const std::vector<std::uint64_t>& tags) {
        std::string feature;
        detail::writeBytes(feature, 2, packed(tags));
        detail::writeVarint(feature, 3 << 3);
        detail::writeVarint(feature, 1); // POINT
        detail::writeBytes(feature, 4, packed({ (1 << 3) | 1, detail::zigzag(100), detail::zigzag(100) }));
        return feature;
    }

    // keys: class, subclass, ramp. values: 'primary', 'minor', 'service', 1.
    // 'service' is only ever a SUBCLASS, so it is in the values table without any class carrying it.
    std::vector<unsigned char> roadTile() {
        std::string layer;
        detail::writeVarint(layer, 15 << 3);
        detail::writeVarint(layer, 2);
        detail::writeBytes(layer, 1, "transportation");
        detail::writeBytes(layer, 2, pointFeature({ 0, 0 }));             // class=primary
        detail::writeBytes(layer, 2, pointFeature({ 0, 1, 1, 2, 2, 3 })); // class=minor subclass=service ramp=1
        detail::writeBytes(layer, 2, pointFeature({ 0, 1 }));             // class=minor
        for (const char* key : { "class", "subclass", "ramp" }) {
            detail::writeBytes(layer, 3, key);
        }
        detail::writeBytes(layer, 4, stringValue("primary"));
        detail::writeBytes(layer, 4, stringValue("minor"));
        detail::writeBytes(layer, 4, stringValue("service"));
        detail::writeBytes(layer, 4, intValue(1));
        detail::writeVarint(layer, 5 << 3);
        detail::writeVarint(layer, 4096);
        std::string tile;
        detail::writeBytes(tile, 3, layer);
        return std::vector<unsigned char>(tile.begin(), tile.end());
    }

    mvt::Predicate fieldEquals(const std::string& field, mvt::Value value) {
        return std::make_shared<mvt::ComparisonPredicate>(mvt::ComparisonPredicate::Op::EQ, std::make_shared<mvt::VariableExpression>(field), mvt::Expression(std::move(value)));
    }
}

void testFieldValuePrefilter() {
    mvt::MBVTFeatureDecoder decoder(roadTile(), std::make_shared<NullLogger>());

    TEST_CHECK(decoder.mayHaveFieldValue("transportation", "class", mvt::Value(std::string("primary"))), "a class the tile layer carries can match");
    TEST_CHECK(!decoder.mayHaveFieldValue("transportation", "class", mvt::Value(std::string("motorway"))), "a class missing from the tile layer cannot match");
    TEST_CHECK(!decoder.mayHaveFieldValue("transportation", "class", mvt::Value(std::string("service"))), "a value carried only under ANOTHER key does not count for this one");
    TEST_CHECK(decoder.mayHaveFieldValue("transportation", "subclass", mvt::Value(std::string("service"))), "the same value under its own key can match");
    TEST_CHECK(decoder.mayHaveFieldValue("transportation", "ramp", mvt::Value(1.0)), "an integer tag matches a double literal, as the filter itself compares");
    TEST_CHECK(!decoder.mayHaveFieldValue("water", "class", mvt::Value(std::string("lake"))), "a layer the tile lacks matches nothing");

    int asked = 0;
    mvt::PredicateFieldValueEvaluator evaluator([&decoder, &asked](const std::string& field, const mvt::Value& value) {
        asked++;
        return decoder.mayHaveFieldValue("transportation", field, value);
    });
    auto motorway = fieldEquals("class", mvt::Value(std::string("motorway")));
    auto minor = fieldEquals("class", mvt::Value(std::string("minor")));
    TEST_CHECK(!std::visit(evaluator, mvt::Predicate(std::make_shared<mvt::AndPredicate>(motorway, minor))), "an AND with one impossible side is impossible");
    TEST_CHECK(std::visit(evaluator, mvt::Predicate(std::make_shared<mvt::OrPredicate>(motorway, minor))), "an OR with one possible side stays possible");
    TEST_CHECK(std::visit(evaluator, mvt::Predicate(std::make_shared<mvt::NotPredicate>(minor))), "a negation is never ruled out from the values alone");
    TEST_CHECK(std::visit(evaluator, mvt::Predicate(std::make_shared<mvt::ComparisonPredicate>(mvt::ComparisonPredicate::Op::NEQ, std::make_shared<mvt::VariableExpression>(std::string("class")), mvt::Expression(mvt::Value(std::string("motorway")))))), "an inequality is never ruled out");
    TEST_CHECK(!std::visit(evaluator, mvt::Predicate(std::make_shared<mvt::ComparisonPredicate>(mvt::ComparisonPredicate::Op::EQ, mvt::Expression(mvt::Value(std::string("motorway"))), std::make_shared<mvt::VariableExpression>(std::string("class"))))), "the literal may stand on either side");

    asked = 0;
    TEST_CHECK(std::visit(evaluator, fieldEquals("name", mvt::Value())), "a test for null can hold on any feature lacking the field");
    TEST_CHECK(std::visit(evaluator, mvt::Predicate(std::make_shared<mvt::ComparisonPredicate>(mvt::ComparisonPredicate::Op::EQ, std::make_shared<mvt::VariableExpression>(std::string("class")), std::make_shared<mvt::VariableExpression>(std::string("param::selected"))))), "a field compared with a style parameter is left alone: a selection repaints without a decode");
    TEST_CHECK(std::visit(evaluator, fieldEquals("param::variant", mvt::Value(std::string("eink")))), "a style parameter is not a feature field");
    TEST_CHECK(asked == 0, "none of those consulted the tile's values");

    // Two passes over the layer with different field sets, then the first set again: the first
    // set's feature data must come back from the cache rather than be rebuilt
    std::set<std::string> classOnly { "class" }, classAndRamp { "class", "ramp" };
    auto firstData = [&decoder](const std::set<std::string>& fields) {
        auto it = decoder.createLayerFeatureIterator("transportation", &fields);
        it->advance(); // class=minor subclass=service ramp=1
        return it->getFeatureData(false, &fields);
    };
    std::shared_ptr<const mvt::FeatureData> before = firstData(classOnly);
    std::shared_ptr<const mvt::FeatureData> other = firstData(classAndRamp);
    std::shared_ptr<const mvt::FeatureData> after = firstData(classOnly);
    TEST_CHECK(before == after, "a field set's feature data survives a pass with another set");
    mvt::Value value;
    TEST_CHECK(before->getVariable("class", value) && !before->getVariable("ramp", value) && !before->getVariable("subclass", value), "feature data holds only the fields asked for");
    TEST_CHECK(other->getVariable("ramp", value) && !other->getVariable("subclass", value), "a wider field set reads the wider data");

    // Two subsets asked through ONE iterator, alternating, as processLayer does per feature
    std::set<std::string> rampOnly { "ramp" };
    auto it = decoder.createLayerFeatureIterator("transportation", &classAndRamp);
    it->advance();
    std::shared_ptr<const mvt::FeatureData> classData = it->getFeatureData(false, &classOnly);
    std::shared_ptr<const mvt::FeatureData> rampData = it->getFeatureData(false, &rampOnly);
    TEST_CHECK(classData->getVariable("class", value) && !classData->getVariable("ramp", value), "the filter subset keeps its own fields");
    TEST_CHECK(rampData->getVariable("ramp", value) && !rampData->getVariable("class", value), "the symbolizer subset keeps its own fields");
    it->advance(); // class=minor alone
    TEST_CHECK(it->getFeatureData(false, &classOnly) == classData, "features with the same values share one feature data, which the rule cache is keyed by");
}
