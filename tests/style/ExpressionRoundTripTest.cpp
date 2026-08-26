/*
 * The mapnik XML round trip: a style compiled by css2xml is written out as expression STRINGS and
 * read back by the decoder, so anything the generator spells ambiguously changes the map. Filters
 * are where it shows - a rule that over-matches paints its colour over every other class.
 */

#include "TestCheck.h"

#include <mapnikvt/Feature.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/ExpressionUtils.h>
#include <mapnikvt/GeneratorUtils.h>
#include <mapnikvt/ParserUtils.h>
#include <mapnikvt/ValueConverter.h>

#include <memory>
#include <string>
#include <vector>

namespace mvt = massif::mvt;

namespace {
    // Comparing the two TREES is not possible - Expression is a variant of shared_ptrs and has no
    // deep equality - and comparing the two STRINGS is worse than useless here: the mis-generated
    // form is its own fixed point, so a text round trip passes while the meaning has changed.
    // What has to survive is the TRUTH TABLE, so evaluate both over every combination of the
    // fields they read.
    bool evaluate(const mvt::Expression& expr, const std::vector<std::string>& fields, unsigned int bits) {
        std::vector<std::pair<std::string, mvt::Value>> vars;
        for (std::size_t i = 0; i < fields.size(); i++) {
            vars.emplace_back(fields[i], mvt::Value(static_cast<long long>((bits >> i) & 1)));
        }
        mvt::ExpressionContext context;
        context.setFeatureData(std::make_shared<mvt::FeatureData>(
            1, mvt::FeatureData::GeometryType::LINE_GEOMETRY, std::move(vars)));
        mvt::Value result = std::visit(mvt::ExpressionEvaluator(context, nullptr), expr);
        return mvt::ValueConverter<bool>::convert(result);
    }

    void checkRoundTrip(const std::string& source, const std::vector<std::string>& fields, const char* what) {
        mvt::Expression expr = mvt::parseExpression(source, false);
        std::string generated = mvt::generateExpressionString(expr, false);
        mvt::Expression reparsed = mvt::parseExpression(generated, false);

        bool same = true;
        for (unsigned int bits = 0; bits < (1u << fields.size()); bits++) {
            if (evaluate(expr, fields, bits) != evaluate(reparsed, fields, bits)) {
                same = false;
            }
        }
        if (!same) {
            std::printf("      %s\n        -> %s   (differs on some feature)\n", source.c_str(), generated.c_str());
        }
        TEST_CHECK(same, what);
    }

    const std::vector<std::string> ABCD = { "a", "b", "c", "d" };

    // The truth table cannot see WHICH alternative the generator picked, and that is the whole
    // hazard: two of them spell the same tree differently and boost.spirit.karma chose between
    // them differently under emcc than under clang, so every native build looked right while the
    // WASM one corrupted filters. Pinning the exact string is what catches that.
    void checkGenerates(const std::string& source, const std::string& expected, const char* what) {
        std::string out = mvt::generateExpressionString(mvt::parseExpression(source, false), false);
        if (out != expected) {
            std::printf("      %s\n        got      %s\n        expected %s\n", source.c_str(), out.c_str(), expected.c_str());
        }
        TEST_CHECK(out == expected, what);
    }
}

void testExpressionRoundTrip() {
    // 'and' and 'or' have the SAME precedence in the parser and associate left to right, so a
    // nested 'or' on the right of an 'and' only survives inside parentheses. Without them
    // `a and (b or c) and d` came back as `((a and b) or c) and d`, which is how every converted
    // MapTiler road rule ended up matching every road class and the whole network drew yellow.
    checkRoundTrip("[a] = 1 and ([b] = 1 or [c] = 1)", ABCD, "an or nested in an and keeps its meaning");
    checkRoundTrip("[a] = 1 and ([b] = 1 or [c] = 1) and [d] = 1", ABCD, "and an or between two ands");
    checkRoundTrip("[a] = 1 or ([b] = 1 and [c] = 1)", ABCD, "an and nested in an or");
    checkRoundTrip("[a] = 1 and ([b] = 1 or [c] = 1 or [d] = 1)", ABCD, "a three-way or nested in an and");

    // The shape the converter emits for a null-safe test, which is what surfaced this.
    checkRoundTrip("[a] = 1 and ([b] = 0 or !([b] <> null))", ABCD, "a null-safe test beside a class test");

    // What already worked has to keep working.
    checkRoundTrip("([a] = 1 or [b] = 1) and [c] = 1", ABCD, "an or on the left of an and");
    checkRoundTrip("[a] = 1 and [b] = 1 and [c] = 1", ABCD, "a flat and chain");
    checkRoundTrip("!([a] = 1)", ABCD, "a negated comparison");
    checkRoundTrip("[a] <> null", ABCD, "a null comparison");

    // The parentheses above are load-bearing, not decoration: this is the spelling the round trip
    // must never produce, and it means something else. `massif-style css2xml` DID produce it for
    // every converted MapTiler road rule - every class matched and the whole network drew yellow -
    // which is how the stale wasm artifact in tools/style-cli/wasm/ was caught.
    bool differs = false;
    mvt::Expression grouped = mvt::parseExpression("[a] = 1 and ([b] = 1 or [c] = 1) and [d] = 1", false);
    mvt::Expression flat = mvt::parseExpression("[a] = 1 and [b] = 1 or [c] = 1 and [d] = 1", false);
    for (unsigned int bits = 0; bits < 16; bits++) {
        differs = differs || evaluate(grouped, ABCD, bits) != evaluate(flat, ABCD, bits);
    }
    TEST_CHECK(differs, "dropping the parentheses would change the meaning");

    // And the parentheses have to be written by the grammar, not left to whichever alternative
    // karma happens to take.
    checkGenerates("[t] = 2 and (([b] = 1) || (!([b] != null)))",
                   "[{'t'}]=2 and ([{'b'}]=1 or !([{'b'}]<>null))",
                   "an or inside an and is parenthesised, and so is a negated comparison");
    checkGenerates("[a] = 1 and ([b] = 1 || [c] = 1 || [d] = 1)",
                   "[{'a'}]=1 and ([{'b'}]=1 or [{'c'}]=1 or [{'d'}]=1)",
                   "a multi-way or keeps one pair of parentheses, not none and not nested ones");
}
