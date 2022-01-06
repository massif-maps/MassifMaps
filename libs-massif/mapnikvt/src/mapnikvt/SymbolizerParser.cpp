#include "Map.h"
#include "SymbolizerParser.h"
#include "PointSymbolizer.h"
#include "LineSymbolizer.h"
#include "LinePatternSymbolizer.h"
#include "PolygonSymbolizer.h"
#include "PolygonPatternSymbolizer.h"
#include "BuildingSymbolizer.h"
#include "MarkersSymbolizer.h"
#include "TextSymbolizer.h"
#include "ShieldSymbolizer.h"
#include "ParserUtils.h"
#include "Logger.h"

#include <boost/algorithm/string.hpp>

namespace carto::mvt {
    std::shared_ptr<Symbolizer> SymbolizerParser::parseSymbolizer(const pugi::xml_node& node, const std::shared_ptr<Map>& map) const {
        std::shared_ptr<Symbolizer> symbolizer = createSymbolizer(node, map);
        if (!symbolizer) {
            return symbolizer;
        }
        
        pugi::xpath_node_set parameterNodes = pugi::xpath_query("CssParameter").evaluate_node_set(node);
        for (pugi::xpath_node_set::const_iterator parameterIt = parameterNodes.begin(); parameterIt != parameterNodes.end(); ++parameterIt) {
            pugi::xml_node parameterNode = (*parameterIt).node();
            std::string parameterName = boost::replace_all_copy(std::string(parameterNode.attribute("name").as_string()), "_", "-");
            std::string parameterValue = parameterNode.text().as_string();
            setSymbolizerParameter(*symbolizer, parameterName, parameterValue);
        }
        for (pugi::xml_attribute_iterator attrIt = node.attributes().begin(); attrIt != node.attributes().end(); ++attrIt) {
            pugi::xml_attribute attr = *attrIt;
            std::string parameterName = boost::replace_all_copy(std::string(attr.name()), "_", "-");
            std::string parameterValue = attr.as_string();
            setSymbolizerParameter(*symbolizer, parameterName, parameterValue);
        }

        return symbolizer;
    }
    
    std::shared_ptr<Symbolizer> SymbolizerParser::createSymbolizer(const pugi::xml_node& node, const std::shared_ptr<Map>& map) const {
        std::string type = node.name();

        std::shared_ptr<Symbolizer> symbolizer;
        if (type == "PointSymbolizer") {
            symbolizer = std::make_shared<PointSymbolizer>(_logger);
        }
        else if (type == "LineSymbolizer") {
            symbolizer = std::make_shared<LineSymbolizer>(_logger);
        }
        else if (type == "LinePatternSymbolizer") {
            symbolizer = std::make_shared<LinePatternSymbolizer>(_logger);
        }
        else if (type == "PolygonSymbolizer") {
            symbolizer = std::make_shared<PolygonSymbolizer>(_logger);
        }
        else if (type == "PolygonPatternSymbolizer") {
            symbolizer = std::make_shared<PolygonPatternSymbolizer>(_logger);
        }
        else if (type == "BuildingSymbolizer") {
            symbolizer = std::make_shared<BuildingSymbolizer>(_logger);
        }
        else if (type == "MarkersSymbolizer") {
            symbolizer = std::make_shared<MarkersSymbolizer>(_logger);
        }
        else if (type == "ShieldSymbolizer" || type == "TextSymbolizer") {
            Expression text = Value(std::string());
            if (!node.text().empty()) {
                text = parseExpression(node.text().as_string(), true);
            }
            if (type == "ShieldSymbolizer") {
                symbolizer = std::make_shared<ShieldSymbolizer>(text, map->getFontSets(), _logger);
            }
            else if (type == "TextSymbolizer") {
                symbolizer = std::make_shared<TextSymbolizer>(text, map->getFontSets(), _logger);
            }
        }
        else {
            _logger->write(Logger::Severity::WARNING, "Unsupported symbolizer type: " + type);
        }
        return symbolizer;
    }

    void SymbolizerParser::setSymbolizerParameter(Symbolizer& symbolizer, const std::string& paramName, const std::string& paramValue) const {
        try {
            if (auto param = symbolizer.getParameter(paramName)) {
                bool stringParam = !dynamic_cast<ValueParameter*>(param) && !dynamic_cast<BoolParameter*>(param) && !dynamic_cast<FloatParameter*>(param) && !dynamic_cast<FloatFunctionParameter*>(param);
                param->setExpression(parseExpression(paramValue, stringParam));
            }
        }
        catch (const std::runtime_error& ex) {
            _logger->write(mvt::Logger::Severity::ERROR, ex.what());
        }
    }
}
