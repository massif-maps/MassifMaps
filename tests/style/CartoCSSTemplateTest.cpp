/*
 * CartoCSS templates (`%name { }`, `@extend %name;`) and `display: none`: what an extending
 * project needs to widen or narrow a base style's rules. See docs/internals/cartocss-templates.md.
 *
 * Checked on the compiler's output, the property sets the translator turns into mapnik rules in
 * order, the first matching one drawing a feature.
 */

#include "TestCheck.h"

#include <cartocss/CartoCSSCompiler.h>
#include <cartocss/CartoCSSParser.h>

#include <list>
#include <map>
#include <string>
#include <utility>

namespace css = massif::css;

namespace {
    std::list<css::AttachmentPropertySets> compileAt(const std::string& source, int zoom) {
        css::StyleSheet styleSheet = css::CartoCSSParser::parse(source);
        std::map<std::pair<int, int>, std::list<css::AttachmentPropertySets>> layerZoomAttachments;
        std::map<std::string, css::Value> constants;
        css::CartoCSSCompiler compiler;
        compiler.compileLayer(styleSheet, "poi", 0, 25, layerZoomAttachments, constants);
        for (const auto& zoomAttachments : layerZoomAttachments) {
            if (zoom >= zoomAttachments.first.first && zoom < zoomAttachments.first.second) {
                return zoomAttachments.second;
            }
        }
        return {};
    }

    const std::list<css::PropertySet>& propertySetsAt(const std::list<css::AttachmentPropertySets>& attachments) {
        static const std::list<css::PropertySet> none;
        return attachments.empty() ? none : attachments.front().getPropertySets();
    }

    std::string valueOf(const css::PropertySet& propertySet, const std::string& field) {
        std::shared_ptr<const css::Property> prop = propertySet.findProperty(field);
        const css::Value* val = prop ? std::get_if<css::Value>(&prop->getExpression()) : nullptr;
        if (!val) {
            return prop ? "<expr>" : "";
        }
        if (auto num = std::get_if<long long>(val)) {
            return std::to_string(*num);
        }
        if (auto str = std::get_if<std::string>(val)) {
            return *str;
        }
        return "<value>";
    }

    bool throws(const std::string& source) {
        try {
            compileAt(source, 15);
        }
        catch (const std::exception&) {
            return true;
        }
        return false;
    }

    const std::string BADGE = "%badge { text-name: [name]; text-face-name: 'medium'; text-size: 12; }\n";
}

void testCartoCSSTemplate() {
    // 1. A rule extending a template gets its declarations; its own win, wherever the @extend is.
    {
        auto sets = propertySetsAt(compileAt(BADGE + "#poi[zoom >= 15]::poi { text-size: 14; @extend %badge; }", 15));
        TEST_CHECK(sets.size() == 1, "an extending rule compiles to one property set");
        TEST_CHECK(!sets.empty() && valueOf(sets.front(), "text-face-name") == "medium", "it carries the template's declarations");
        TEST_CHECK(!sets.empty() && valueOf(sets.front(), "text-size") == "14", "and its own declaration beats the template's");
    }

    // 2. Widening: an extending project shows a class before the base rule does. Without the template
    // the new rule had no text-name, built no symbolizer and was dropped.
    {
        std::string source = BADGE +
            "#poi[zoom >= 17][rank >= 20]::poi { @extend %badge; }\n"
            "#poi[class = 'bakery'][zoom >= 14]::poi { @extend %badge; text-placement-priority: 5; }\n";
        auto z15 = propertySetsAt(compileAt(source, 15));
        TEST_CHECK(z15.size() == 1 && valueOf(z15.front(), "text-name") == "<expr>", "the widened class is drawn where the base is not");
        TEST_CHECK(!z15.empty() && valueOf(z15.front(), "text-placement-priority") == "5", "with the override's own priority");

        auto z17 = propertySetsAt(compileAt(source, 17));
        bool baseKept = false;
        for (const css::PropertySet& set : z17) {
            baseKept = baseKept || (set.getFilters().size() == 1 && valueOf(set, "text-placement-priority").empty());
        }
        TEST_CHECK(baseKept, "the base rule still draws every other class");
    }

    // 3. The last definition of a template wins, so an extending project can restyle the base.
    {
        auto sets = propertySetsAt(compileAt(BADGE + "#poi[zoom >= 15]::poi { @extend %badge; }\n%badge { text-name: [name]; text-face-name: 'bold'; }\n", 15));
        TEST_CHECK(!sets.empty() && valueOf(sets.front(), "text-face-name") == "bold", "a redefined template restyles the rules extending it");
    }

    // 4. Narrowing: `display: none` keeps a rule with nothing to draw, first in line for the class.
    {
        std::string source = BADGE +
            "#poi[zoom >= 15]::poi { @extend %badge; }\n"
            "#poi[class = 'bus'][zoom < 17]::poi { display: none; }\n";
        auto z15 = propertySetsAt(compileAt(source, 15));
        TEST_CHECK(z15.size() == 2 && z15.front().isSuppressed(), "the suppressed class comes first at z15");
        TEST_CHECK(z15.size() == 2 && !z15.back().isSuppressed(), "the base rule follows it for everything else");

        auto z17 = propertySetsAt(compileAt(source, 17));
        TEST_CHECK(z17.size() == 1 && !z17.front().isSuppressed(), "and nothing is suppressed past the override's zoom");

        auto shown = propertySetsAt(compileAt(source + "#poi[class = 'bus'][zoom >= 15]::poi { display: auto; }\n", 15));
        bool anySuppressed = false;
        for (const css::PropertySet& set : shown) {
            anySuppressed = anySuppressed || set.isSuppressed();
        }
        TEST_CHECK(!anySuppressed, "a later rule can show the class again");
    }

    // 5. Mistakes fail the load instead of drawing nothing.
    TEST_CHECK(throws("#poi[zoom >= 15]::poi { @extend %missing; }"), "an undefined template is an error");
    TEST_CHECK(throws("%a { @extend %b; } %b { @extend %a; } #poi[zoom >= 15]::poi { @extend %a; }"), "a template cycle is an error");
}
