#include "CartoCSSMapnikTranslator.h"
#include "Expression.h"
#include "Predicate.h"
#include "CartoCSSParser.h"
#include "CartoCSSCompiler.h"
#include "mapnikvt/Expression.h"
#include "mapnikvt/Predicate.h"
#include "mapnikvt/Filter.h"
#include "mapnikvt/Rule.h"
#include "mapnikvt/Style.h"
#include "mapnikvt/Layer.h"
#include "mapnikvt/PointSymbolizer.h"
#include "mapnikvt/LineSymbolizer.h"
#include "mapnikvt/LinePatternSymbolizer.h"
#include "mapnikvt/PolygonSymbolizer.h"
#include "mapnikvt/PolygonPatternSymbolizer.h"
#include "mapnikvt/TextSymbolizer.h"
#include "mapnikvt/MarkersSymbolizer.h"
#include "mapnikvt/ShieldSymbolizer.h"
#include "mapnikvt/BuildingSymbolizer.h"
#include "mapnikvt/GeneratorUtils.h"

namespace carto { namespace css {
    std::shared_ptr<mvt::Rule> CartoCSSMapnikTranslator::buildRule(const PropertySet& propertySet, const std::shared_ptr<mvt::Map>& map, int minZoom, int maxZoom) const {
        std::vector<Property> properties = propertySet.getProperties();
        std::sort(properties.begin(), properties.end(), [](const Property& prop1, const Property& prop2) {
            return prop1.getRuleOrder() < prop2.getRuleOrder();
        });

        std::list<std::pair<std::string, std::list<Property>>> propertyLists;
        for (auto it = properties.begin(); it != properties.end(); it++) {
            std::string symbolizerId = getPropertySymbolizerId(it->getField());
            if (symbolizerId.empty()) { // layer-level property? (comp-op, opacity, image-filters)
                continue;
            }
            auto it2 = std::find_if(propertyLists.begin(), propertyLists.end(), [&](const std::pair<std::string, std::list<Property>>& propertyListElement) { return propertyListElement.first == symbolizerId; });
            if (it2 == propertyLists.end()) {
                it2 = propertyLists.insert(it2, { symbolizerId, std::list<Property>() });
            }
            it2->second.push_back(*it);
        }

        std::vector<std::shared_ptr<mvt::Symbolizer>> mapnikSymbolizers;
        for (const std::pair<std::string, std::list<Property>>& propertyListElement : propertyLists) {
            std::string symbolizerId = propertyListElement.first;
            std::string symbolizerType = symbolizerId.substr(symbolizerId.rfind('/') + 1);
            std::shared_ptr<mvt::Symbolizer> mapnikSymbolizer = buildSymbolizer(symbolizerType, propertyListElement.second, map);
            if (mapnikSymbolizer) {
                mapnikSymbolizers.push_back(mapnikSymbolizer);
            }
        }

        std::optional<mvt::Predicate> mapnikFilterPred;
        for (const std::shared_ptr<Predicate>& pred : propertySet.getFilters()) {
            std::optional<mvt::Predicate> mapnikPred = buildPredicate(*pred);
            if (mapnikPred) {
                if (mapnikFilterPred) {
                    mapnikFilterPred = std::make_shared<mvt::AndPredicate>(*mapnikFilterPred, *mapnikPred);
                }
                else {
                    mapnikFilterPred = mapnikPred;
                }
            }
        }
        auto mapnikFilter = std::make_shared<mvt::Filter>(mvt::Filter::Type::FILTER, mapnikFilterPred);

        return std::make_shared<mvt::Rule>("auto", minZoom, maxZoom, mapnikFilter, mapnikSymbolizers);
    }

    std::shared_ptr<mvt::Symbolizer> CartoCSSMapnikTranslator::buildSymbolizer(const std::string& symbolizerType, const std::list<Property>& properties, const std::shared_ptr<mvt::Map>& map) const {
        std::shared_ptr<mvt::Symbolizer> mapnikSymbolizer;
        if (symbolizerType == "point") {
            mapnikSymbolizer = std::make_shared<mvt::PointSymbolizer>(_logger);
        }
        else if (symbolizerType == "line") {
            mapnikSymbolizer = std::make_shared<mvt::LineSymbolizer>(_logger);
        }
        else if (symbolizerType == "line-pattern") {
            mapnikSymbolizer = std::make_shared<mvt::LinePatternSymbolizer>(_logger);
        }
        else if (symbolizerType == "polygon") {
            mapnikSymbolizer = std::make_shared<mvt::PolygonSymbolizer>(_logger);
        }
        else if (symbolizerType == "polygon-pattern") {
            mapnikSymbolizer = std::make_shared<mvt::PolygonPatternSymbolizer>(_logger);
        }
        else if (symbolizerType == "marker") {
            mapnikSymbolizer = std::make_shared<mvt::MarkersSymbolizer>(_logger);
        }
        else if (symbolizerType == "text" || symbolizerType == "shield") {
            std::string text;
            std::pair<std::string, std::string> fontSetFaceName;
            for (const Property& prop : properties) {
                if (prop.getField() == symbolizerType + "-name") {
                    try {
                        if (auto val = std::get_if<Value>(&prop.getExpression())) {
                            text = mvt::ValueConverter<std::string>::convert(buildValue(*val));
                        }
                        else {
                            text = buildExpressionString(prop.getExpression(), true);
                        }
                    }
                    catch (const std::exception& ex) {
                        _logger->write(mvt::Logger::Severity::ERROR, "Error while parsing text expression in " + symbolizerType + " symbolizer: " + ex.what());
                        return std::shared_ptr<mvt::Symbolizer>();
                    }
                }
                else if (prop.getField() == symbolizerType + "-face-name") {
                    if (auto val = std::get_if<Value>(&prop.getExpression())) {
                        std::string faceName = std::get<std::string>(*val);
                        fontSetFaceName = std::pair<std::string, std::string>("face-name", faceName);
                    }
                    else if (auto listExpr = std::get_if<std::shared_ptr<ListExpression>>(&prop.getExpression())) {
                        std::string fontSetName;
                        std::vector<std::string> faceNames;
                        for (const Expression& faceNameExpr : (*listExpr)->getExpressions()) {
                            if (auto val = std::get_if<Value>(&faceNameExpr)) {
                                std::string faceName = std::get<std::string>(*val);
                                fontSetName += (fontSetName.empty() ? "" : ",") + faceName;
                                faceNames.push_back(faceName);
                            }
                            else {
                                _logger->write(mvt::Logger::Severity::WARNING, "Expecting constant value for face name property");
                            }
                        }
                        if (!map->getFontSet(fontSetName)) {
                            auto fontSet = std::make_shared<mvt::FontSet>(fontSetName, faceNames);
                            map->addFontSet(fontSet);
                        }
                        fontSetFaceName = std::pair<std::string, std::string>("fontset-name", fontSetName);
                    }
                    else {
                        _logger->write(mvt::Logger::Severity::WARNING, "Expecting constant value or list for face name property");
                    }
                }
            }
            if (!text.empty()) {
                if (symbolizerType == "text") {
                    mapnikSymbolizer = std::make_shared<mvt::TextSymbolizer>(text, map->getFontSets(), _logger);
                }
                else if (symbolizerType == "shield") {
                    mapnikSymbolizer = std::make_shared<mvt::ShieldSymbolizer>(text, map->getFontSets(), _logger);
                }
                if (mapnikSymbolizer && !fontSetFaceName.first.empty()) {
                    mapnikSymbolizer->setParameter(fontSetFaceName.first, fontSetFaceName.second);
                }
            }
            else {
                // No need to warn, it is legal to have no 'text'
                return std::shared_ptr<mvt::Symbolizer>();
            }
        }
        else if (symbolizerType == "building") {
            mapnikSymbolizer = std::make_shared<mvt::BuildingSymbolizer>(_logger);
        }
        else {
            _logger->write(mvt::Logger::Severity::ERROR, "Unsupported symbolizer type: " + symbolizerType);
            return std::shared_ptr<mvt::Symbolizer>();
        }

        for (const Property& prop : properties) {
            std::string propertyId = prop.getField().substr(prop.getField().rfind('/') + 1);
            auto it = _symbolizerPropertyMap.find(propertyId);
            if (it == _symbolizerPropertyMap.end()) {
                _logger->write(mvt::Logger::Severity::WARNING, "Unsupported symbolizer property: " + propertyId);
                continue;
            }
            try {
                setSymbolizerParameter(mapnikSymbolizer, it->second, prop.getExpression(), isStringExpression(propertyId));
            }
            catch (const std::exception& ex) {
                _logger->write(mvt::Logger::Severity::ERROR, "Error while setting " + propertyId + " parameter: " + ex.what());
            }
        }

        return mapnikSymbolizer;
    }

    std::string CartoCSSMapnikTranslator::buildValueString(const Value& value, bool stringExpr) {
        if (stringExpr) {
            return mvt::ValueConverter<std::string>::convert(buildValue(value));
        }
        return mvt::generateValueString(buildValue(value));
    }

    std::string CartoCSSMapnikTranslator::buildExpressionString(const Expression& expr, bool stringExpr) {
        struct ExpressionBuilder {
            explicit ExpressionBuilder(bool stringExpr) : _stringExpr(stringExpr) { }
            
            std::string operator() (const Value& val) const { return buildValueString(val, _stringExpr); }
            std::string operator() (const FieldOrVar& fieldOrVar) const {
                if (!fieldOrVar.isField()) {
                    throw TranslatorException("FieldOrVar: expecting field, not variable (@" + fieldOrVar.getName() + ")");
                }
                return "[" + fieldOrVar.getName() + "]";
            }
            
            std::string operator() (const std::shared_ptr<ListExpression>& listExpr) const {
                std::string exprStr;
                for (const Expression& subExpr : listExpr->getExpressions()) {
                    if (!exprStr.empty()) {
                        exprStr += ",";
                    }
                    exprStr += (_stringExpr ? "{" : "") + buildExpressionString(subExpr, false) + (_stringExpr ? "}" : "");
                }
                return exprStr;
            }

            std::string operator() (const std::shared_ptr<UnaryExpression>& unaryExpr) const {
                std::string subExprStr = buildExpressionString(unaryExpr->getExpression(), false);
                auto it = std::find_if(_unaryOpTable.begin(), _unaryOpTable.end(), [unaryExpr](const std::pair<UnaryExpression::Op, std::string>& item) { return item.first == unaryExpr->getOp(); });
                if (it == _unaryOpTable.end()) {
                    throw TranslatorException("Unsupported unary operator type");
                }
                std::string exprStr = it->second + "(" + subExprStr + ")";
                return (_stringExpr ? "{" : "") + exprStr + (_stringExpr ? "}" : "");
            }

            std::string operator() (const std::shared_ptr<BinaryExpression>& binaryExpr) const {
                std::string subExpr1Str = buildExpressionString(binaryExpr->getExpression1(), false);
                std::string subExpr2Str = buildExpressionString(binaryExpr->getExpression2(), false);
                auto it = std::find_if(_binaryOpTable.begin(), _binaryOpTable.end(), [binaryExpr](const std::pair<BinaryExpression::Op, std::string>& item) { return item.first == binaryExpr->getOp(); });
                if (it == _binaryOpTable.end()) {
                    throw TranslatorException("Unsupported binary operator type");
                }
                std::string exprStr = "(" + subExpr1Str + ")" + it->second + "(" + subExpr2Str + ")";
                return (_stringExpr ? "{" : "") + exprStr + (_stringExpr ? "}" : "");
            }

            std::string operator() (const std::shared_ptr<ConditionalExpression>& condExpr) const {
                std::string condStr = buildExpressionString(condExpr->getCondition(), false);
                std::string subExpr1Str = buildExpressionString(condExpr->getExpression1(), false);
                std::string subExpr2Str = buildExpressionString(condExpr->getExpression2(), false);
                std::string exprStr = "(" + condStr + ")" + " ? " + "(" + subExpr1Str + ")" + " : " + "(" + subExpr2Str + ")";
                return (_stringExpr ? "{" : "") + exprStr + (_stringExpr ? "}" : "");
            }

            std::string operator() (const std::shared_ptr<FunctionExpression>& funcExpr) const {
                return buildFunctionExpressionString(*funcExpr, _stringExpr);
            }

        private:
            bool _stringExpr;
        };

        return std::visit(ExpressionBuilder(stringExpr), expr);
    }

    std::string CartoCSSMapnikTranslator::buildFunctionExpressionString(const FunctionExpression& funcExpr, bool stringExpr) {
        if (_stringFuncs.find(funcExpr.getFunc()) != _stringFuncs.end()) {
            // Convert to built-in function with 'dot' notation like [name].length
            int arity = _stringFuncs.find(funcExpr.getFunc())->second;
            if (funcExpr.getArgs().size() != arity) {
                throw TranslatorException("Wrong number of arguments for string function");
            }

            std::string exprStr = "(" + buildExpressionString(funcExpr.getArgs()[0], false) + ")" + "." + funcExpr.getFunc();
            if (funcExpr.getArgs().size() > 1) {
                exprStr += "(";
                for (std::size_t i = 1; i < funcExpr.getArgs().size(); i++) {
                    if (i > 1) {
                        exprStr += ",";
                    }

                    exprStr += buildExpressionString(funcExpr.getArgs()[i], false);
                }
                exprStr += ")";
            }
            return  (stringExpr ? "{" : "") + exprStr + (stringExpr ? "}" : "");
        }
        else if (_mathFuncs.find(funcExpr.getFunc()) != _mathFuncs.end()) {
            // Convert to normal built-in function like pow([zoom], 2)
            int arity = _mathFuncs.find(funcExpr.getFunc())->second;
            if (funcExpr.getArgs().size() != arity) {
                throw TranslatorException("Wrong number of arguments for math function");
            }

            std::string exprStr = funcExpr.getFunc();
            exprStr += "(";
            for (std::size_t i = 0; i < funcExpr.getArgs().size(); i++) {
                if (i > 0) {
                    exprStr += ",";
                }

                exprStr += buildExpressionString(funcExpr.getArgs()[i], false);
            }
            exprStr += ")";
            return  (stringExpr ? "{" : "") + exprStr + (stringExpr ? "}" : "");
        }
        else if (_interpolationFuncs.find(funcExpr.getFunc()) != _interpolationFuncs.end()) {
            // Interpolation function. Variable arity, special case.
            if (funcExpr.getArgs().size() < 2) {
                throw TranslatorException("Expecting at least two arguments for interpolation function");
            }
            
            bool colorMode = false, scalarMode = false;
            std::vector<std::pair<Value, Value>> keyFrames;
            for (std::size_t i = 1; i < funcExpr.getArgs().size(); i++) {
                auto listExpr = std::get_if<std::shared_ptr<ListExpression>>(&funcExpr.getArgs()[i]);
                if (!listExpr) {
                    throw TranslatorException("Expecting interpolation list");
                }
                if ((*listExpr)->getExpressions().size() != 2) {
                    throw TranslatorException("Expecting interpolation elements of size 2");
                }
                auto keyVal = std::get_if<Value>(&(*listExpr)->getExpressions()[0]);
                if (!keyVal) {
                    throw TranslatorException("Expecting constant key");
                }
                auto valueVal = std::get_if<Value>(&(*listExpr)->getExpressions()[1]);
                if (!valueVal) {
                    throw TranslatorException("Expecting constant value");
                }
                if (std::holds_alternative<Color>(*valueVal)) {
                    colorMode = true;
                }
                else {
                    scalarMode = true;
                }
                keyFrames.emplace_back(*keyVal, *valueVal);
            }

            if (colorMode == scalarMode) {
                throw TranslatorException("Ambiguous interpolation values");
            }

            if (colorMode) {
                // Color interpolation. Split color values into 4 components and create separate interpolators for each
                std::string exprStr = "rgba";
                exprStr += "(";
                for (std::size_t c = 0; c < 4; c++) {
                    std::string subExprStr = funcExpr.getFunc();
                    subExprStr += "(";
                    subExprStr += buildExpressionString(funcExpr.getArgs()[0], false);
                    std::set<float> keyFrameComponents;
                    for (const std::pair<Value, Value>& keyFrame : keyFrames) {
                        float component = std::get<Color>(keyFrame.second).rgba()[c] * (c < 3 ? 255.0f : 1.0f);
                        subExprStr += "," + mvt::ValueConverter<std::string>::convert(buildValue(keyFrame.first));
                        subExprStr += "," + mvt::ValueConverter<std::string>::convert(mvt::Value(component));
                        keyFrameComponents.insert(component);
                    }
                    subExprStr += ")";

                    if (c > 0) {
                        exprStr += ",";
                    }

                    // Do trivial optimization, if only single keyframe is specified
                    if (keyFrameComponents.size() == 1) {
                        exprStr += buildValueString(*keyFrameComponents.begin(), stringExpr);
                    }
                    else {
                        exprStr += (stringExpr ? "{" : "") + subExprStr + (stringExpr ? "}" : "");
                    }
                }
                exprStr += ")";
                return exprStr;
            }
            else {
                // Scalar interpolation
                std::string exprStr = funcExpr.getFunc();
                exprStr += "(";
                exprStr += (stringExpr ? "{" : "") + buildExpressionString(funcExpr.getArgs()[0], false) + (stringExpr ? "}" : "");
                std::set<Value> keyFrameValues;
                for (const std::pair<Value, Value>& keyFrame : keyFrames) {
                    Value value = keyFrame.second;
                    exprStr += "," + mvt::ValueConverter<std::string>::convert(buildValue(keyFrame.first));
                    exprStr += "," + mvt::ValueConverter<std::string>::convert(buildValue(value));
                    keyFrameValues.insert(value);
                }
                exprStr += ")";

                // Do trivial optimization, if only single keyframe is specified
                if (keyFrameValues.size() == 1) {
                    return buildValueString(*keyFrameValues.begin(), stringExpr);
                }

                return exprStr;
            }
        }
        else {
            // Assume pseudo-function (like 'translate(1,2)')
            std::string exprStr = funcExpr.getFunc();
            exprStr += "(";
            for (std::size_t i = 0; i < funcExpr.getArgs().size(); i++) {
                if (i > 0) {
                    exprStr += ",";
                }

                if (auto val = std::get_if<Value>(&funcExpr.getArgs()[i])) {
                    exprStr += buildValueString(*val, stringExpr);
                }
                else {
                    exprStr += (stringExpr ? "{" : "") + buildExpressionString(funcExpr.getArgs()[i], false) + (stringExpr ? "}" : "");
                }
            }
            exprStr += ")";
            return exprStr;
        }
    }

    std::optional<mvt::Predicate> CartoCSSMapnikTranslator::buildPredicate(const Predicate& pred) {
        struct PredicateBuilder {
            std::optional<mvt::Predicate> operator() (const MapPredicate& mapPred) const { return std::optional<mvt::Predicate>(); }
            std::optional<mvt::Predicate> operator() (const LayerPredicate& layerPred) const { return std::optional<mvt::Predicate>(); }
            std::optional<mvt::Predicate> operator() (const AttachmentPredicate& attachmentPred) const { return std::optional<mvt::Predicate>(); }

            std::optional<mvt::Predicate> operator() (const ClassPredicate& classPred) const {
                mvt::Value val = buildValue(classPred.getClass());
                return std::make_shared<mvt::ComparisonPredicate>(buildComparisonOp(OpPredicate::Op::EQ), std::make_shared<mvt::VariableExpression>(std::string("class")), val);
            }

            std::optional<mvt::Predicate> operator() (const OpPredicate& opPred) const {
                if (!opPred.getFieldOrVar().isField()) {
                    throw TranslatorException("OpPredicate: expecting field, not variable (@" + opPred.getFieldOrVar().getName() + ")");
                }
                std::string var = opPred.getFieldOrVar().getName();
                mvt::Value val = buildValue(opPred.getRefValue());
                return std::make_shared<mvt::ComparisonPredicate>(buildComparisonOp(opPred.getOp()), std::make_shared<mvt::VariableExpression>(var), val);
            }
        };

        return std::visit(PredicateBuilder(), pred);
    }

    mvt::ComparisonPredicate::Op CartoCSSMapnikTranslator::buildComparisonOp(OpPredicate::Op op) {
        auto it = std::find_if(_predicateOpTable.begin(), _predicateOpTable.end(), [op](const std::pair<OpPredicate::Op, mvt::ComparisonPredicate::Op>& item) { return item.first == op; });
        if (it == _predicateOpTable.end()) {
            throw TranslatorException("Unsupported predicate operator");
        }
        return it->second;
    }

    mvt::Value CartoCSSMapnikTranslator::buildValue(const Value& val) {
        struct ValueBuilder {
            mvt::Value operator() (std::monostate) const { return mvt::Value(); }
            mvt::Value operator() (bool val) const { return mvt::Value(val); }
            mvt::Value operator() (long long val) const { return mvt::Value(val); }
            mvt::Value operator() (double val) const { return mvt::Value(val); }
            mvt::Value operator() (const Color& val) const { std::stringstream ss; ss << val; return mvt::Value(ss.str()); }
            mvt::Value operator() (const std::string& val) const { return mvt::Value(val); }
        };

        return std::visit(ValueBuilder(), val);
    }

    bool CartoCSSMapnikTranslator::isStringExpression(const std::string& propertyName) const {
        std::string propertyType = propertyName.substr(propertyName.rfind('-') + 1);
        return _symbolizerNonStringProperties.find(propertyType) == _symbolizerNonStringProperties.end();
    }

    std::string CartoCSSMapnikTranslator::getPropertySymbolizerId(const std::string& propertyName) const {
        std::string::size_type symbolizerTypePos = propertyName.rfind('/') + 1;
        for (const std::string& symbolizerType : _symbolizerList) {
            if (propertyName.substr(symbolizerTypePos, symbolizerType.size()) == symbolizerType) {
                return propertyName.substr(0, symbolizerTypePos + symbolizerType.size());
            }
        }
        return std::string();
    }

    void CartoCSSMapnikTranslator::setSymbolizerParameter(const std::shared_ptr<mvt::Symbolizer>& symbolizer, const std::string& name, const Expression& expr, bool stringExpr) const {
        if (!name.empty()) {
            std::string exprStr;
            if (auto val = std::get_if<Value>(&expr)) {
                exprStr = mvt::ValueConverter<std::string>::convert(buildValue(*val));
            }
            else {
                exprStr = buildExpressionString(expr, stringExpr);
            }
            symbolizer->setParameter(name, exprStr);
        }
    }

    const std::vector<std::pair<UnaryExpression::Op, std::string>> CartoCSSMapnikTranslator::_unaryOpTable = {
        { UnaryExpression::Op::NEG, "-" },
        { UnaryExpression::Op::NOT, "!" }
    };

    const std::vector<std::pair<BinaryExpression::Op, std::string>> CartoCSSMapnikTranslator::_binaryOpTable = {
        { BinaryExpression::Op::AND, "&&" },
        { BinaryExpression::Op::OR,  "||" },
        { BinaryExpression::Op::EQ,  "="  },
        { BinaryExpression::Op::NEQ, "!=" },
        { BinaryExpression::Op::LT,  "<"  },
        { BinaryExpression::Op::LTE, "<=" },
        { BinaryExpression::Op::GT,  ">"  },
        { BinaryExpression::Op::GTE, ">=" },
        { BinaryExpression::Op::ADD, "+"  },
        { BinaryExpression::Op::SUB, "-"  },
        { BinaryExpression::Op::MUL, "*"  },
        { BinaryExpression::Op::DIV, "/"  },
        { BinaryExpression::Op::MATCH, ".match" }
    };

    const std::vector<std::pair<OpPredicate::Op, mvt::ComparisonPredicate::Op>> CartoCSSMapnikTranslator::_predicateOpTable = {
        { OpPredicate::Op::EQ,  mvt::ComparisonPredicate::Op::EQ  },
        { OpPredicate::Op::NEQ, mvt::ComparisonPredicate::Op::NEQ },
        { OpPredicate::Op::LT,  mvt::ComparisonPredicate::Op::LT  },
        { OpPredicate::Op::LTE, mvt::ComparisonPredicate::Op::LTE },
        { OpPredicate::Op::GT,  mvt::ComparisonPredicate::Op::GT  },
        { OpPredicate::Op::GTE, mvt::ComparisonPredicate::Op::GTE },
        { OpPredicate::Op::MATCH, mvt::ComparisonPredicate::Op::MATCH }
    };

    const std::unordered_map<std::string, int> CartoCSSMapnikTranslator::_stringFuncs = {
        { "uppercase",  1 },
        { "lowercase",  1 },
        { "captialize", 1 },
        { "length",     1 },
        { "concat",     2 },
        { "match",      2 },
        { "replace",    3 }
    };
    
    const std::unordered_map<std::string, int> CartoCSSMapnikTranslator::_mathFuncs = {
        { "exp", 1 },
        { "log", 1 },
        { "pow", 2 }
    };
    
    const std::unordered_set<std::string>  CartoCSSMapnikTranslator::_interpolationFuncs = {
        "step",
        "linear",
        "cubic"
    };

    const std::vector<std::string> CartoCSSMapnikTranslator::_symbolizerList = {
        "line-pattern",
        "line",
        "polygon-pattern",
        "polygon",
        "point",
        "text",
        "marker",
        "shield",
        "building"
    };

    const std::unordered_set<std::string> CartoCSSMapnikTranslator::_symbolizerNonStringProperties = {
        "width",
        "height",
        "size",
        "opacity",
        "radius",
        "distance",
        "spacing",
        "orientation",
        "priority",
        "dx",
        "dy"
    };

    const std::unordered_map<std::string, std::string> CartoCSSMapnikTranslator::_symbolizerPropertyMap = {
        { "line-color", "stroke" },
        { "line-opacity", "stroke-opacity" },
        { "line-width", "stroke-width" },
        { "line-dasharray", "stroke-dasharray" },
        { "line-join", "stroke-linejoin" },
        { "line-cap", "stroke-linecap" },
        { "line-geometry-transform", "geometry-transform" },
        { "line-comp-op", "comp-op" },

        { "line-pattern-file", "file" },
        { "line-pattern-fill", "fill" },
        { "line-pattern-opacity", "opacity" },
        { "line-pattern-geometry-transform", "geometry-transform" },
        { "line-pattern-comp-op", "comp-op" },

        { "polygon-fill", "fill" },
        { "polygon-opacity", "fill-opacity" },
        { "polygon-geometry-transform", "geometry-transform" },
        { "polygon-comp-op", "comp-op" },

        { "polygon-pattern-file", "file" },
        { "polygon-pattern-fill", "fill" },
        { "polygon-pattern-opacity", "opacity" },
        { "polygon-pattern-geometry-transform", "geometry-transform" },
        { "polygon-pattern-comp-op", "comp-op" },

        { "point-file", "file" },
        { "point-opacity", "opacity" },
        { "point-allow-overlap", "allow-overlap" },
        { "point-ignore-placement", "ignore-placement" },
        { "point-placement-priority", "placement-priority" },
        { "point-transform", "transform" },
        { "point-comp-op", "comp-op" },

        { "text-name", "" },
        { "text-feature-id", "feature-id" },
        { "text-face-name", "" },
        { "text-placement", "placement" },
        { "text-size", "size" },
        { "text-spacing", "spacing" },
        { "text-fill", "fill" },
        { "text-opacity", "opacity" },
        { "text-halo-fill", "halo-fill" },
        { "text-halo-opacity", "halo-opacity" },
        { "text-halo-radius", "halo-radius" },
        { "text-halo-rasterizer", "halo-rasterizer" },
        { "text-allow-overlap", "allow-overlap" },
        { "text-min-distance", "minimum-distance" },
        { "text-transform", "text-transform" },
        { "text-orientation", "orientation" },
        { "text-dx", "dx" },
        { "text-dy", "dy" },
        { "text-avoid-edges", "avoid-edges" },
        { "text-wrap-width", "wrap-width" },
        { "text-wrap-before", "wrap-before" },
        { "text-character-spacing", "character-spacing" },
        { "text-line-spacing", "line-spacing" },
        { "text-horizontal-alignment", "horizontal-alignment" },
        { "text-vertical-alignment", "vertical-alignment" },
        { "text-placement-priority", "placement-priority" },
        { "text-comp-op", "comp-op" },
        { "text-clip", "clip" },

        { "shield-name", "" },
        { "shield-feature-id", "feature-id" },
        { "shield-face-name", "" },
        { "shield-file", "file" },
        { "shield-dx", "shield-dx" },
        { "shield-dy", "shield-dy" },
        { "shield-unlock-image", "unlock-image" },
        { "shield-placement", "placement" },
        { "shield-size", "size" },
        { "shield-spacing", "spacing" },
        { "shield-fill", "fill" },
        { "shield-text-opacity", "opacity" },
        { "shield-halo-fill", "halo-fill" },
        { "shield-halo-opacity", "halo-opacity" },
        { "shield-halo-radius", "halo-radius" },
        { "shield-halo-rasterizer", "halo-rasterizer" },
        { "shield-allow-overlap", "allow-overlap" },
        { "shield-min-distance", "minimum-distance" },
        { "shield-text-transform", "text-transform" },
        { "shield-orientation", "orientation" },
        { "shield-text-dx", "dx" },
        { "shield-text-dy", "dy" },
        { "shield-avoid-edges", "avoid-edges" },
        { "shield-wrap-width", "wrap-width" },
        { "shield-wrap-before", "wrap-before" },
        { "shield-character-spacing", "character-spacing" },
        { "shield-line-spacing", "line-spacing" },
        { "shield-horizontal-alignment", "horizontal-alignment" },
        { "shield-vertical-alignment", "vertical-alignment" },
        { "shield-placement-priority", "placement-priority" },
        { "shield-comp-op", "comp-op" },
        { "shield-clip", "clip" },

        { "marker-file", "file" },
        { "marker-placement", "placement" },
        { "marker-type", "marker-type" },
        { "marker-feature-id", "feature-id" },
        { "marker-opacity", "opacity" },
        { "marker-fill", "fill" },
        { "marker-fill-opacity", "fill-opacity" },
        { "marker-width", "width" },
        { "marker-height", "height" },
        { "marker-line-color", "stroke" },
        { "marker-line-opacity", "stroke-opacity" },
        { "marker-line-width", "stroke-width" },
        { "marker-spacing", "spacing" },
        { "marker-allow-overlap", "allow-overlap" },
        { "marker-ignore-placement", "ignore-placement" },
        { "marker-placement-priority", "placement-priority" },
        { "marker-transform", "transform" },
        { "marker-comp-op", "comp-op" },
        { "marker-clip", "clip" },

        { "building-fill", "fill" },
        { "building-fill-opacity", "fill-opacity" },
        { "building-height", "height" },
        { "building-min-height", "min-height" },
        { "building-geometry-transform", "geometry-transform" }
        // NOTE: comp-op not supported for building symbolizer
    };
} }
