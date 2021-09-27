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
    std::shared_ptr<const mvt::Rule> CartoCSSMapnikTranslator::buildRule(const PropertySet& propertySet, const std::shared_ptr<mvt::Map>& map, int minZoom, int maxZoom) const {
        std::vector<std::shared_ptr<const Property>> properties = propertySet.getProperties();
        std::sort(properties.begin(), properties.end(), [](const std::shared_ptr<const Property>& prop1, const std::shared_ptr<const Property>& prop2) {
            return prop1->getRuleOrder() < prop2->getRuleOrder();
        });

        std::list<std::pair<std::string, std::vector<std::shared_ptr<const Property>>>> propertyLists;
        for (auto it = properties.begin(); it != properties.end(); it++) {
            std::string symbolizerId = getPropertySymbolizerId((*it)->getField());
            if (symbolizerId.empty()) { // layer-level property? (comp-op, opacity, image-filters)
                continue;
            }
            auto it2 = std::find_if(propertyLists.begin(), propertyLists.end(), [&](const std::pair<std::string, std::vector<std::shared_ptr<const Property>>>& propertyListElement) { return propertyListElement.first == symbolizerId; });
            if (it2 == propertyLists.end()) {
                it2 = propertyLists.insert(it2, { symbolizerId, std::vector<std::shared_ptr<const Property>>() });
            }
            it2->second.push_back(*it);
        }

        std::vector<std::shared_ptr<const mvt::Symbolizer>> mapnikSymbolizers;
        for (const std::pair<std::string, std::vector<std::shared_ptr<const Property>>>& propertyListElement : propertyLists) {
            std::string symbolizerId = propertyListElement.first;
            std::string symbolizerType = symbolizerId.substr(symbolizerId.rfind('/') + 1);
            std::shared_ptr<const mvt::Symbolizer> mapnikSymbolizer = buildSymbolizer(symbolizerType, propertyListElement.second, map);
            if (mapnikSymbolizer) {
                mapnikSymbolizers.push_back(mapnikSymbolizer);
            }
        }

        std::shared_ptr<const mvt::Filter> mapnikFilter = buildFilter(propertySet.getFilters());

        return std::make_shared<mvt::Rule>("auto", minZoom, maxZoom, std::move(mapnikFilter), std::move(mapnikSymbolizers));
    }

    std::shared_ptr<const mvt::Filter> CartoCSSMapnikTranslator::buildFilter(const std::vector<std::shared_ptr<const Predicate>>& filters) const {
        FilterKey key;
        key.filters = filters;

        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _filterCache.find(key);
        if (it != _filterCache.end()) {
            return it->second;
        }
        std::optional<mvt::Predicate> mapnikFilterPred;
        for (const std::shared_ptr<const Predicate>& pred : filters) {
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
        std::shared_ptr<const mvt::Filter> filter = std::make_shared<mvt::Filter>(mvt::Filter::Type::FILTER, mapnikFilterPred);
        _filterCache[key] = filter;
        return filter;
    }

    std::shared_ptr<const mvt::Symbolizer> CartoCSSMapnikTranslator::buildSymbolizer(const std::string& symbolizerType, const std::vector<std::shared_ptr<const Property>>& properties, const std::shared_ptr<mvt::Map>& map) const {
        SymbolizerKey key;
        key.symbolizerType = symbolizerType;
        key.map = map;
        key.properties.assign(properties.begin(), properties.end());
        std::sort(key.properties.begin(), key.properties.end(), [](const std::shared_ptr<const Property>& prop1, const std::shared_ptr<const Property>& prop2) {
            return prop1->getField() < prop2->getField();
        });

        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _symbolizerCache.find(key);
        if (it != _symbolizerCache.end()) {
            return it->second;
        }
        std::shared_ptr<const mvt::Symbolizer> symbolizer = createSymbolizer(symbolizerType, properties, map);
        _symbolizerCache[key] = symbolizer;
        return symbolizer;
    }

    mvt::Expression CartoCSSMapnikTranslator::buildExpression(const Expression& expr) {
        struct ExpressionBuilder {
            mvt::Expression operator() (const Value& val) const {
                return buildValue(val);
            }
            
            mvt::Expression operator() (const FieldOrVar& fieldOrVar) const {
                if (!fieldOrVar.isField()) {
                    throw TranslatorException("Undefined variable in expression (@" + fieldOrVar.getName() + ")");
                }
                try {
                    return std::make_shared<mvt::VariableExpression>(mvt::parseExpression(fieldOrVar.getName(), true));
                }
                catch (const std::exception& ex) {
                    throw TranslatorException("Failed to parse variable expression: " + std::string(ex.what()));
                }
            }

            mvt::Expression operator() (const std::shared_ptr<StringExpression>& strExpr) const {
                try {
                    return mvt::parseExpression(strExpr->getString(), true);
                }
                catch (const std::exception& ex) {
                    throw TranslatorException("Failed to parse string expression: " + std::string(ex.what()));
                }
            }

            mvt::Expression operator() (const std::shared_ptr<ListExpression>& listExpr) const {
                std::string str;
                for (const Expression& listSubExpr : listExpr->getExpressions()) {
                    mvt::Expression mapnikExpr = std::visit(*this, listSubExpr);
                    if (auto val = std::get_if<mvt::Value>(&mapnikExpr)) {
                        str += (str.empty() ? "" : ",") + mvt::ValueConverter<std::string>::convert(*val);
                    }
                    else {
                        throw TranslatorException("List expressions may only contain constant values");
                    }
                }
                return mvt::Value(std::move(str));
            }

            mvt::Expression operator() (const std::shared_ptr<UnaryExpression>& unaryExpr) const {
                mvt::Expression mapnikExpr = std::visit(*this, unaryExpr->getExpression());

                if (unaryExpr->getOp() == UnaryExpression::Op::NOT) {
                    return std::make_shared<mvt::NotPredicate>(std::make_shared<mvt::ExpressionPredicate>(std::move(mapnikExpr)));
                }
                
                auto it = std::find_if(_unaryOpTable.begin(), _unaryOpTable.end(), [unaryExpr](const std::pair<UnaryExpression::Op, mvt::UnaryExpression::Op>& item) { return item.first == unaryExpr->getOp(); });
                if (it != _unaryOpTable.end()) {
                    return std::make_shared<mvt::UnaryExpression>(it->second, std::move(mapnikExpr));
                }
                throw TranslatorException("Unsupported unary operator type");
            }

            mvt::Expression operator() (const std::shared_ptr<BinaryExpression>& binaryExpr) const {
                mvt::Expression mapnikExpr1 = std::visit(*this, binaryExpr->getExpression1());
                mvt::Expression mapnikExpr2 = std::visit(*this, binaryExpr->getExpression2());

                if (binaryExpr->getOp() == BinaryExpression::Op::AND) {
                    return std::make_shared<mvt::AndPredicate>(std::make_shared<mvt::ExpressionPredicate>(std::move(mapnikExpr1)), std::make_shared<mvt::ExpressionPredicate>(std::move(mapnikExpr2)));
                }
                else if (binaryExpr->getOp() == BinaryExpression::Op::OR) {
                    return std::make_shared<mvt::OrPredicate>(std::make_shared<mvt::ExpressionPredicate>(std::move(mapnikExpr1)), std::make_shared<mvt::ExpressionPredicate>(std::move(mapnikExpr2)));
                }

                auto it1 = std::find_if(_comparisonOpTable.begin(), _comparisonOpTable.end(), [binaryExpr](const std::pair<BinaryExpression::Op, mvt::ComparisonPredicate::Op>& item) { return item.first == binaryExpr->getOp(); });
                if (it1 != _comparisonOpTable.end()) {
                    return std::make_shared<mvt::ComparisonPredicate>(it1->second, std::move(mapnikExpr1), std::move(mapnikExpr2));
                }

                auto it2 = std::find_if(_binaryOpTable.begin(), _binaryOpTable.end(), [binaryExpr](const std::pair<BinaryExpression::Op, mvt::BinaryExpression::Op>& item) { return item.first == binaryExpr->getOp(); });
                if (it2 != _binaryOpTable.end()) {
                    return std::make_shared<mvt::BinaryExpression>(it2->second, std::move(mapnikExpr1), std::move(mapnikExpr2));
                }
                throw TranslatorException("Unsupported binary operator type");
            }

            mvt::Expression operator() (const std::shared_ptr<ConditionalExpression>& condExpr) const {
                mvt::Expression mapnikCond  = std::visit(*this, condExpr->getCondition());
                mvt::Expression mapnikExpr1 = std::visit(*this, condExpr->getExpression1());
                mvt::Expression mapnikExpr2 = std::visit(*this, condExpr->getExpression2());
                return std::make_shared<mvt::TertiaryExpression>(mvt::TertiaryExpression::Op::CONDITIONAL, std::move(mapnikCond), std::move(mapnikExpr1), std::move(mapnikExpr2));
            }

            mvt::Expression operator() (const std::shared_ptr<FunctionExpression>& funcExpr) const {
                return buildFunctionExpression(*funcExpr);
            }
        };

        return std::visit(ExpressionBuilder(), expr);
    }

    mvt::Expression CartoCSSMapnikTranslator::buildFunctionExpression(const FunctionExpression& funcExpr) {
        if (auto funcIt = _basicFuncs.find(funcExpr.getFunc()); funcIt != _basicFuncs.end()) {
            // Basic functions. Fixed arity.
            struct FunctionBuilder {
                explicit FunctionBuilder(const std::vector<mvt::Expression>& exprs) : _exprs(exprs) { }
                
                mvt::Expression operator() (const mvt::UnaryExpression::Op op) const {
                    if (_exprs.size() != 1) {
                        throw TranslatorException("Wrong number of arguments for unary function");
                    }
                    return std::make_shared<mvt::UnaryExpression>(op, _exprs[0]);
                }

                mvt::Expression operator() (const mvt::BinaryExpression::Op op) const {
                    if (_exprs.size() != 2) {
                        throw TranslatorException("Wrong number of arguments for binary function");
                    }
                    return std::make_shared<mvt::BinaryExpression>(op, _exprs[0], _exprs[1]);
                }

                mvt::Expression operator() (const mvt::TertiaryExpression::Op op) const {
                    if (_exprs.size() != 3) {
                        throw TranslatorException("Wrong number of arguments for tertiary function");
                    }
                    return std::make_shared<mvt::TertiaryExpression>(op, _exprs[0], _exprs[1], _exprs[2]);
                }

            private:
                const std::vector<mvt::Expression>& _exprs;
            };

            std::vector<mvt::Expression> mapnikExprs;
            mapnikExprs.reserve(funcExpr.getArgs().size());
            for (const Expression& expr : funcExpr.getArgs()) {
                mapnikExprs.push_back(buildExpression(expr));
            }
            return std::visit(FunctionBuilder(mapnikExprs), funcIt->second);
        }
        else if (auto funcIt = _interpolationFuncs.find(funcExpr.getFunc()); funcIt != _interpolationFuncs.end()) {
            // Interpolation function. Variable arity, special case.
            std::size_t argCount = funcExpr.getArgs().size();
            if (argCount < 2) {
                throw TranslatorException("Expecting at least two arguments for interpolation function");
            }

            mvt::Expression mapnikTimeExpr = buildExpression(funcExpr.getArgs()[0]);

            std::vector<mvt::Value> mapnikKeyFrames;
            mapnikKeyFrames.reserve((argCount - 1) * 2);
            for (std::size_t i = 1; i < argCount; i++) {
                auto listExpr = std::get_if<std::shared_ptr<ListExpression>>(&funcExpr.getArgs()[i]);
                if (!listExpr) {
                    throw TranslatorException("Expecting element list for interpolation function");
                }
                if ((*listExpr)->getExpressions().size() != 2) {
                    throw TranslatorException("Expecting elements of size 2 for interpolation function");
                }
                auto mapnikKeyExpr = buildExpression((*listExpr)->getExpressions()[0]);
                auto keyVal = std::get_if<mvt::Value>(&mapnikKeyExpr);
                if (!keyVal) {
                    throw TranslatorException("Expecting constant scalar keys for interpolation function");
                }
                auto mapnikValueExpr = buildExpression((*listExpr)->getExpressions()[1]);
                auto valueVal = std::get_if<mvt::Value>(&mapnikValueExpr);
                if (!valueVal) {
                    throw TranslatorException("Expecting constant scalar values for interpolation function");
                }
                mapnikKeyFrames.push_back(*keyVal);
                mapnikKeyFrames.push_back(*valueVal);
            }
            return std::make_shared<mvt::InterpolateExpression>(funcIt->second, std::move(mapnikTimeExpr), std::move(mapnikKeyFrames));
        }
        else {
            // Assume pseudo-function (like 'translate(1,2)')
            std::string exprStr = funcExpr.getFunc();
            exprStr += "(";
            const char* argSeparator = "";
            for (const Expression& expr : funcExpr.getArgs()) {
                mvt::Expression mapnikExpr = buildExpression(expr);
                if (auto val = std::get_if<mvt::Value>(&mapnikExpr)) {
                    exprStr += argSeparator + mvt::generateValueString(*val);
                }
                else {
                    throw TranslatorException("Expecting constant arguments for function " + funcExpr.getFunc());
                }
                argSeparator = ",";
            }
            exprStr += ")";
            return mvt::Value(std::move(exprStr));
        }
    }

    std::optional<mvt::Predicate> CartoCSSMapnikTranslator::buildPredicate(const Predicate& pred) {
        struct PredicateBuilder {
            std::optional<mvt::Predicate> operator() (const MapPredicate& mapPred) const { return std::optional<mvt::Predicate>(); }
            std::optional<mvt::Predicate> operator() (const LayerPredicate& layerPred) const { return std::optional<mvt::Predicate>(); }
            std::optional<mvt::Predicate> operator() (const AttachmentPredicate& attachmentPred) const { return std::optional<mvt::Predicate>(); }

            std::optional<mvt::Predicate> operator() (const ClassPredicate& classPred) const {
                mvt::Value val = buildValue(classPred.getClass());
                return std::make_shared<mvt::ComparisonPredicate>(buildComparisonOp(OpPredicate::Op::EQ), std::make_shared<mvt::VariableExpression>(std::string("class")), std::move(val));
            }

            std::optional<mvt::Predicate> operator() (const OpPredicate& opPred) const {
                if (!opPred.getFieldOrVar().isField()) {
                    throw TranslatorException("Undefined variable in predicate (@" + opPred.getFieldOrVar().getName() + ")");
                }
                std::string var = opPred.getFieldOrVar().getName();
                mvt::Value val = buildValue(opPred.getRefValue());
                return std::make_shared<mvt::ComparisonPredicate>(buildComparisonOp(opPred.getOp()), std::make_shared<mvt::VariableExpression>(std::move(var)), std::move(val));
            }

            std::optional<mvt::Predicate> operator() (const WhenPredicate& whenPred) const {
                mvt::Expression mapnikExpr = buildExpression(whenPred.getExpression());
                return std::make_shared<mvt::ExpressionPredicate>(std::move(mapnikExpr));
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

    std::string CartoCSSMapnikTranslator::getPropertySymbolizerId(const std::string& propertyName) const {
        std::string::size_type symbolizerTypePos = propertyName.rfind('/') + 1;
        for (const std::string& symbolizerType : _symbolizerList) {
            if (propertyName.substr(symbolizerTypePos, symbolizerType.size()) == symbolizerType) {
                return propertyName.substr(0, symbolizerTypePos + symbolizerType.size());
            }
        }
        return std::string();
    }

    std::shared_ptr<const mvt::Symbolizer> CartoCSSMapnikTranslator::createSymbolizer(const std::string& symbolizerType, const std::vector<std::shared_ptr<const Property>>& properties, const std::shared_ptr<mvt::Map>& map) const {
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
            // Extact text expression and font name or font set name
            mvt::Expression mapnikTextExpr = mvt::Value(std::string());
            std::pair<std::string, std::string> fontSetFaceName;
            for (const std::shared_ptr<const Property>& prop : properties) {
                if (prop->getField() == symbolizerType + "-name") {
                    mapnikTextExpr = buildExpression(prop->getExpression());
                }
                else if (prop->getField() == symbolizerType + "-face-name") {
                    if (auto listExpr = std::get_if<std::shared_ptr<ListExpression>>(&prop->getExpression())) {
                        std::string fontSetName;
                        std::vector<std::string> faceNames;
                        for (const Expression& listSubExpr : (*listExpr)->getExpressions()) {
                            mvt::Expression mapnikExpr = buildExpression(listSubExpr);
                            if (auto val = std::get_if<mvt::Value>(&mapnikExpr)) {
                                std::string faceName = mvt::ValueConverter<std::string>::convert(*val);
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
                        mvt::Expression mapnikExpr = buildExpression(prop->getExpression());
                        if (auto val = std::get_if<mvt::Value>(&mapnikExpr)) {
                            std::string faceName = mvt::ValueConverter<std::string>::convert(*val);
                            fontSetFaceName = std::pair<std::string, std::string>("face-name", faceName);
                        }
                        else {
                            _logger->write(mvt::Logger::Severity::WARNING, "Expecting constant value or list for face name property");
                        }
                    }
                }
            }

            // Check if the text expression is not empty.
            if (mapnikTextExpr != mvt::Expression(mvt::Value(std::string()))) {
                if (symbolizerType == "text") {
                    mapnikSymbolizer = std::make_shared<mvt::TextSymbolizer>(mapnikTextExpr, map->getFontSets(), _logger);
                }
                else if (symbolizerType == "shield") {
                    mapnikSymbolizer = std::make_shared<mvt::ShieldSymbolizer>(mapnikTextExpr, map->getFontSets(), _logger);
                }
                if (mapnikSymbolizer && !fontSetFaceName.first.empty()) {
                    try {
                        if (auto param = mapnikSymbolizer->getParameter(fontSetFaceName.first)) {
                            param->setExpression(mvt::Value(fontSetFaceName.second));
                        }
                    }
                    catch (const std::exception& ex) {
                        _logger->write(mvt::Logger::Severity::ERROR, "Error while setting " + fontSetFaceName.first + " parameter: " + ex.what());
                    }
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

        for (const std::shared_ptr<const Property>& prop : properties) {
            std::string propertyId = prop->getField().substr(prop->getField().rfind('/') + 1);
            auto it = _symbolizerPropertyMap.find(propertyId);
            if (it == _symbolizerPropertyMap.end()) {
                _logger->write(mvt::Logger::Severity::WARNING, "Unsupported symbolizer property: " + propertyId);
                continue;
            }
            if (it->second.empty()) {
                // Pseudo-parameter, already handled
                continue;
            }

            try {
                mvt::Expression mapnikExpr = buildExpression(prop->getExpression());
                if (auto param = mapnikSymbolizer->getParameter(it->second)) {
                    param->setExpression(mapnikExpr);
                }
            }
            catch (const std::exception& ex) {
                _logger->write(mvt::Logger::Severity::ERROR, "Error while setting " + propertyId + " parameter: " + ex.what());
            }
        }

        return mapnikSymbolizer;
    }

    const std::vector<std::pair<UnaryExpression::Op, mvt::UnaryExpression::Op>> CartoCSSMapnikTranslator::_unaryOpTable = {
        { UnaryExpression::Op::NEG, mvt::UnaryExpression::Op::NEG }
    };

    const std::vector<std::pair<BinaryExpression::Op, mvt::BinaryExpression::Op>> CartoCSSMapnikTranslator::_binaryOpTable = {
        { BinaryExpression::Op::ADD, mvt::BinaryExpression::Op::ADD },
        { BinaryExpression::Op::SUB, mvt::BinaryExpression::Op::SUB },
        { BinaryExpression::Op::MUL, mvt::BinaryExpression::Op::MUL },
        { BinaryExpression::Op::DIV, mvt::BinaryExpression::Op::DIV }
    };

    const std::vector<std::pair<BinaryExpression::Op, mvt::ComparisonPredicate::Op>> CartoCSSMapnikTranslator::_comparisonOpTable = {
        { BinaryExpression::Op::EQ,  mvt::ComparisonPredicate::Op::EQ  },
        { BinaryExpression::Op::NEQ, mvt::ComparisonPredicate::Op::NEQ },
        { BinaryExpression::Op::LT,  mvt::ComparisonPredicate::Op::LT  },
        { BinaryExpression::Op::LTE, mvt::ComparisonPredicate::Op::LTE },
        { BinaryExpression::Op::GT,  mvt::ComparisonPredicate::Op::GT  },
        { BinaryExpression::Op::GTE, mvt::ComparisonPredicate::Op::GTE },
        { BinaryExpression::Op::MATCH, mvt::ComparisonPredicate::Op::MATCH }
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

    const std::unordered_map<std::string, std::variant<mvt::UnaryExpression::Op, mvt::BinaryExpression::Op, mvt::TertiaryExpression::Op>> CartoCSSMapnikTranslator::_basicFuncs = {
        { "exp",        mvt::UnaryExpression::Op::EXP },
        { "log",        mvt::UnaryExpression::Op::LOG },
        { "uppercase",  mvt::UnaryExpression::Op::UPPER },
        { "lowercase",  mvt::UnaryExpression::Op::LOWER },
        { "captialize", mvt::UnaryExpression::Op::CAPITALIZE },
        { "length",     mvt::UnaryExpression::Op::LENGTH },
        { "pow",        mvt::BinaryExpression::Op::POW },
        { "concat",     mvt::BinaryExpression::Op::CONCAT },
        { "replace",    mvt::TertiaryExpression::Op::REPLACE }
    };
    
    const std::unordered_map<std::string, mvt::InterpolateExpression::Method> CartoCSSMapnikTranslator::_interpolationFuncs = {
        { "step",   mvt::InterpolateExpression::Method::STEP },
        { "linear", mvt::InterpolateExpression::Method::LINEAR },
        { "cubic",  mvt::InterpolateExpression::Method::CUBIC }
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

    const std::unordered_map<std::string, std::string> CartoCSSMapnikTranslator::_symbolizerPropertyMap = {
        { "line-color", "stroke" },
        { "line-opacity", "stroke-opacity" },
        { "line-width", "stroke-width" },
        { "line-dasharray", "stroke-dasharray" },
        { "line-join", "stroke-linejoin" },
        { "line-cap", "stroke-linecap" },
        { "line-offset", "offset" },
        { "line-geometry-transform", "geometry-transform" },
        { "line-comp-op", "comp-op" },

        { "line-pattern-file", "file" },
        { "line-pattern-fill", "fill" },
        { "line-pattern-opacity", "opacity" },
        { "line-pattern-offset", "offset" },
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
        { "text-wrap-character", "wrap-character" },
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
        { "shield-wrap-character", "wrap-character" },
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
        { "marker-color", "color" },
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
