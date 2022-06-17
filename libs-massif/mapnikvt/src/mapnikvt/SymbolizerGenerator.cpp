#include "SymbolizerGenerator.h"
#include "PointSymbolizer.h"
#include "LineSymbolizer.h"
#include "LinePatternSymbolizer.h"
#include "PolygonSymbolizer.h"
#include "PolygonPatternSymbolizer.h"
#include "BuildingSymbolizer.h"
#include "MarkersSymbolizer.h"
#include "TextSymbolizer.h"
#include "ShieldSymbolizer.h"
#include "GeneratorUtils.h"
#include "Logger.h"

namespace carto::mvt {
    void SymbolizerGenerator::generateSymbolizer(const Symbolizer& symbolizer, pugi::xml_node& symbolizerNode) const {
        std::string type;
        if (dynamic_cast<const PointSymbolizer*>(&symbolizer)) {
            type = "PointSymbolizer";
        }
        else if (dynamic_cast<const LineSymbolizer*>(&symbolizer)) {
            type = "LineSymbolizer";
        }
        else if (dynamic_cast<const LinePatternSymbolizer*>(&symbolizer)) {
            type = "LinePatternSymbolizer";
        }
        else if (dynamic_cast<const PolygonSymbolizer*>(&symbolizer)) {
            type = "PolygonSymbolizer";
        }
        else if (dynamic_cast<const PolygonPatternSymbolizer*>(&symbolizer)) {
            type = "PolygonPatternSymbolizer";
        }
        else if (dynamic_cast<const BuildingSymbolizer*>(&symbolizer)) {
            type = "BuildingSymbolizer";
        }
        else if (dynamic_cast<const MarkersSymbolizer*>(&symbolizer)) {
            type = "MarkersSymbolizer";
        }
        else if (auto textSymbolizer = dynamic_cast<const TextSymbolizer*>(&symbolizer)) {
            if (dynamic_cast<const ShieldSymbolizer*>(&symbolizer)) {
                type = "ShieldSymbolizer";
            }
            else {
                type = "TextSymbolizer";
            }
        }

        symbolizerNode.set_name(type.c_str());
        for (const std::string& name : symbolizer.getPropertyNames()) {
            if (name == "name" && dynamic_cast<const TextSymbolizer*>(&symbolizer)) {
                continue; // already included as 'content'
            }
            if (auto prop = symbolizer.getProperty(name)) {
                if (prop->isDefined()) {
                    std::string value = getSymbolizerProperty(symbolizer, *prop);
                    symbolizerNode.append_attribute(name.c_str()).set_value(value.c_str());
                }
            }
        }

        if (auto textSymbolizer = dynamic_cast<const TextSymbolizer*>(&symbolizer)) {
            std::string text = generateExpressionString(textSymbolizer->getText(), true);
            symbolizerNode.append_child(pugi::node_pcdata).set_value(text.c_str());
        }
    }

    std::string SymbolizerGenerator::getSymbolizerProperty(const Symbolizer& symbolizer, const Property& prop) const {
        bool stringExpr = !dynamic_cast<const ValueProperty*>(&prop) && !dynamic_cast<const BoolProperty*>(&prop) && !dynamic_cast<const FloatProperty*>(&prop) && !dynamic_cast<const FloatFunctionProperty*>(&prop);
        return generateExpressionString(prop.getExpression(), stringExpr);
    }
}
