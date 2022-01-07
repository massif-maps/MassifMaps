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
        for (const std::string& paramName : symbolizer.getParameterNames()) {
            if (paramName == "name" && dynamic_cast<const TextSymbolizer*>(&symbolizer)) {
                continue; // already included as 'content'
            }
            if (auto param = symbolizer.getParameter(paramName)) {
                if (param->isDefined()) {
                    std::string paramValue = getSymbolizerParameter(symbolizer, *param);
                    symbolizerNode.append_attribute(paramName.c_str()).set_value(paramValue.c_str());
                }
            }
        }

        if (auto textSymbolizer = dynamic_cast<const TextSymbolizer*>(&symbolizer)) {
            std::string text = generateExpressionString(textSymbolizer->getText(), true);
            symbolizerNode.append_child(pugi::node_pcdata).set_value(text.c_str());
        }
    }

    std::string SymbolizerGenerator::getSymbolizerParameter(const Symbolizer& symbolizer, const SymbolizerParameter& param) const {
        bool stringParam = !dynamic_cast<const ValueParameter*>(&param) && !dynamic_cast<const BoolParameter*>(&param) && !dynamic_cast<const FloatParameter*>(&param) && !dynamic_cast<const FloatFunctionParameter*>(&param);
        return generateExpressionString(param.getExpression(), stringParam);
    }
}
