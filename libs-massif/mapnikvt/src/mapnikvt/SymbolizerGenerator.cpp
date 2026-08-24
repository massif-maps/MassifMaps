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
#include "RasterConfigSymbolizer.h"
#include "HillshadeConfigSymbolizer.h"
#include "ContourConfigSymbolizer.h"
#include "GeneratorUtils.h"
#include "Logger.h"

#include <typeinfo>

namespace massif::mvt {
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
        else if (dynamic_cast<const RasterConfigSymbolizer*>(&symbolizer)) {
            type = "RasterConfigSymbolizer";
        }
        else if (dynamic_cast<const HillshadeConfigSymbolizer*>(&symbolizer)) {
            type = "HillshadeConfigSymbolizer";
        }
        else if (dynamic_cast<const ContourConfigSymbolizer*>(&symbolizer)) {
            type = "ContourConfigSymbolizer";
        }
        else if (auto textSymbolizer = dynamic_cast<const TextSymbolizer*>(&symbolizer)) {
            if (dynamic_cast<const ShieldSymbolizer*>(&symbolizer)) {
                type = "ShieldSymbolizer";
            }
            else {
                type = "TextSymbolizer";
            }
        }

        if (type.empty()) {
            // An unnamed node with attributes is XML the parser cannot read back; leave it empty
            // so MapGenerator drops it, and say which symbolizer was lost.
            _logger->write(Logger::Severity::WARNING, std::string("Unsupported symbolizer type: ") + typeid(symbolizer).name());
            return;
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

        // A config symbolizer that sets nothing still declares the slot, and MapGenerator drops a
        // node with no attributes - so give it the one that says the slot is there.
        if (dynamic_cast<const LayerConfigSymbolizer*>(&symbolizer) && symbolizerNode.attributes().empty()) {
            symbolizerNode.append_attribute("visible").set_value(true);
        }
    }

    std::string SymbolizerGenerator::getSymbolizerProperty(const Symbolizer& symbolizer, const Property& prop) const {
        bool stringExpr = !dynamic_cast<const ValueProperty*>(&prop) && !dynamic_cast<const BoolProperty*>(&prop) && !dynamic_cast<const FloatProperty*>(&prop) && !dynamic_cast<const FloatFunctionProperty*>(&prop);
        return generateExpressionString(prop.getExpression(), stringExpr);
    }
}
