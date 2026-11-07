/*
 * The `@road_width_<class>` variables of the Massif SDK project (styles/massif/carto/variables.mss):
 * a child project's rule takes a road's width as `line-gap-width` to draw its own casing.
 *
 * Checked on the compiler's output over the generated file. Not covered: the mapnik translator and a
 * render, so that the casing lines up on screen is a device check.
 */

#include "TestCheck.h"

#include <cartocss/CartoCSSCompiler.h>
#include <cartocss/CartoCSSParser.h>

#include <fstream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace css = massif::css;

namespace {
    std::string readFile(const std::string& path) {
        std::ifstream in(path);
        std::stringstream out;
        out << in.rdbuf();
        return out.str();
    }

    const css::Property* gapWidthAt(const std::string& source, int zoom, std::shared_ptr<const css::Property>& keep) {
        css::StyleSheet styleSheet = css::CartoCSSParser::parse(source);
        std::map<std::pair<int, int>, std::list<css::AttachmentPropertySets>> layerZoomAttachments;
        std::map<std::string, css::Value> constants;
        css::CartoCSSCompiler compiler;
        compiler.compileLayer(styleSheet, "transportation", 0, 25, layerZoomAttachments, constants);
        for (const auto& zoomAttachments : layerZoomAttachments) {
            if (zoom >= zoomAttachments.first.first && zoom < zoomAttachments.first.second) {
                for (const css::AttachmentPropertySets& attachment : zoomAttachments.second) {
                    for (const css::PropertySet& set : attachment.getPropertySets()) {
                        keep = set.findProperty("line-gap-width");
                        if (keep) {
                            return keep.get();
                        }
                    }
                }
            }
        }
        return nullptr;
    }

    const std::string RULE = "#transportation[zoom >= 14][class = 'primary']::child { line-color: #f00; line-width: 2; line-gap-width: @road_width_primary; }\n";
}

void testRoadWidthVariables() {
    const std::string variables = readFile(std::string(MASSIF_STYLE_DIR) + "/carto/variables.mss");
    TEST_CHECK(!variables.empty(), "the generated variables.mss is readable");

    // 1. Every road class has its variable, and a child rule using one compiles to a width.
    for (const char* cls : { "motorway", "trunk", "primary", "secondary", "tertiary", "minor", "service" }) {
        TEST_CHECK(variables.find(std::string("@road_width_") + cls + ":") != std::string::npos, "variables.mss declares a width for each class");
    }
    std::shared_ptr<const css::Property> keep;
    TEST_CHECK(gapWidthAt(variables + RULE, 14, keep) != nullptr, "a rule taking @road_width_primary as line-gap-width compiles");

    // 2. The width is the ramp, not a constant: it has to follow the zoom.
    TEST_CHECK(keep && !std::get_if<css::Value>(&keep->getExpression()), "the width stays a zoom expression");

    // The whole base: the child rule is listed after style.mss, as a project's would be.
    const std::string style = readFile(std::string(MASSIF_STYLE_DIR) + "/carto/style.mss");
    std::shared_ptr<const css::Property> full;
    TEST_CHECK(gapWidthAt(variables + style + RULE, 14, full) != nullptr, "the child rule compiles over the whole generated style");

    // 3. A stylesheet listed before variables.mss redeclares one, as it does a colour.
    std::shared_ptr<const css::Property> own;
    const css::Property* prop = gapWidthAt("@road_width_primary: 7;\n" + variables + RULE, 14, own);
    const css::Value* value = prop ? std::get_if<css::Value>(&prop->getExpression()) : nullptr;
    const long long* number = value ? std::get_if<long long>(value) : nullptr;
    TEST_CHECK(number && *number == 7, "a child redeclaring a width overrides the base's");
}
