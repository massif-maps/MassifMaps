/*
 * Parsing a CartoCSS expression has to stay linear in its nesting depth.
 *
 * `expressionlist` used to be two alternatives that both began with `expression`: parse the head,
 * demand a comma, and on the missing comma backtrack and parse the very same head again. Every
 * parenthesised sub-expression goes through that rule, and a nested ternary is parentheses all the
 * way down, so the cost doubled per level. Measured on device: depth 12 loaded in ~5 s, depth 20 in
 * 40 s, and MapTiler streets-v4's 28-deep road-shield country switch never finished at all.
 */

#include "TestCheck.h"

#include <cartocss/CartoCSSParser.h>
#include <cartocss/Expression.h>

#include <chrono>
#include <string>

namespace css = massif::css;

namespace {
    std::string nestedTernary(int depth) {
        std::string expr = "#ffffff";
        for (int i = depth; i > 0; i--) {
            expr = "(([iso_a2] = 'C" + std::to_string(i) + "') ? #000000 : " + expr + ")";
        }
        return "#road { line-color: " + expr + "; }";
    }

    double parseSeconds(const std::string& source) {
        auto start = std::chrono::steady_clock::now();
        css::StyleSheet styleSheet = css::CartoCSSParser::parse(source);
        auto elapsed = std::chrono::steady_clock::now() - start;
        TEST_CHECK(!styleSheet.getElements().empty(), "nested ternary parses to a rule");
        return std::chrono::duration<double>(elapsed).count();
    }
}

void testCartoCSSParse() {
    // The shape of the bug, not a wall-clock budget: doubling the depth may not more than quadruple
    // the time. Exponential blows straight through that on any machine; linear stays near 2x.
    double shallow = parseSeconds(nestedTernary(10));
    double deep = parseSeconds(nestedTernary(20));
    TEST_CHECK(deep < shallow * 4.0 + 0.05, "parse time does not explode with nesting depth");

    // The depth that hung. A second is orders of magnitude above linear and far below exponential.
    auto start = std::chrono::steady_clock::now();
    css::StyleSheet styleSheet = css::CartoCSSParser::parse(nestedTernary(28));
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    TEST_CHECK(!styleSheet.getElements().empty() && seconds < 1.0, "a 28-deep ternary parses at once");

    // The comma list `expressionlist` exists for still has to parse - the head is read once now,
    // and the tail is what makes it a list.
    css::StyleSheet list = css::CartoCSSParser::parse("#a { line-dasharray: 1, 2, 3; }");
    TEST_CHECK(!list.getElements().empty(), "a comma list still parses");
}
