/*
 * A style's `draw-once` group is an expression, evaluated per tile as TileReader does: the Massif
 * family is one CartoCSS project, and only the hybrid variant names a group for its roads.
 * Not covered: the CartoCSS loader reading the property (not linked here) and the stencil passes.
 */

#include "TestCheck.h"

#include <mapnikvt/Expression.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/ExpressionUtils.h>
#include <mapnikvt/GeneratorUtils.h>
#include <mapnikvt/ParserUtils.h>
#include <mapnikvt/Style.h>
#include <mapnikvt/StyleParameterStore.h>

#include <map>
#include <memory>
#include <optional>
#include <string>

using namespace massif::mvt;

namespace {
    std::string group(const Style& style, const std::string& variant) {
        ExpressionContext context;
        context.setStyleParameterStore(std::make_shared<StyleParameterStore>(std::map<std::string, Value> { { "variant", Value(variant) } }));
        return ValueConverter<std::string>::convert(std::visit(ExpressionEvaluator(context, nullptr), style.getDrawOnce()));
    }

    std::shared_ptr<Style> makeStyle(std::optional<Expression> drawOnce) {
        if (!drawOnce) {
            return std::make_shared<Style>("road", 1.0f, "", std::nullopt, Style::FilterMode::FIRST, "", std::vector<std::shared_ptr<const Rule>>());
        }
        return std::make_shared<Style>("road", 1.0f, "", std::nullopt, Style::FilterMode::FIRST, "", std::vector<std::shared_ptr<const Rule>>(), *drawOnce);
    }
}

void testDrawOnce() {
    TEST_CHECK(group(*makeStyle(std::nullopt), "hybrid").empty(), "a style that states no group draws plain");

    Expression picked = parseExpression("([param::variant] = 'hybrid') ? 'road' : ''", false);
    TEST_CHECK(group(*makeStyle(picked), "hybrid") == "road", "the hybrid variant names the road group");
    TEST_CHECK(group(*makeStyle(picked), "streets").empty(), "every other variant names none, so draws as before");

    // The compiled XML's `draw-once` attribute: written and read back as a string expression.
    Expression reread = parseExpression(generateExpressionString(picked, true), true);
    TEST_CHECK(group(*makeStyle(reread), "hybrid") == "road" && group(*makeStyle(reread), "topo").empty(),
               "the group survives the XML round trip per variant");
    Expression constant = parseExpression(generateExpressionString(Value(std::string("road")), true), true);
    TEST_CHECK(group(*makeStyle(constant), "streets") == "road", "a constant group survives it too");
}
