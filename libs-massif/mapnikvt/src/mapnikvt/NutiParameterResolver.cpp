#include "NutiParameterResolver.h"
#include "Expression.h"
#include "ExpressionContext.h"
#include "ExpressionUtils.h"
#include "Filter.h"
#include "LineSymbolizer.h"
#include "Map.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "Properties.h"
#include "Rule.h"
#include "Style.h"
#include "Symbolizer.h"
#include "ValueConverter.h"

#include <algorithm>

namespace carto::mvt {
    namespace {
        // Every comparison in an expression tree, predicates and the expressions inside them alike.
        // The variable visitors only surface variables, and a fold has to see the comparison itself.
        struct ComparisonCollector {
            explicit ComparisonCollector(std::vector<std::shared_ptr<ComparisonPredicate>>& comparisons) : _comparisons(comparisons) { }

            void operator() (bool val) const { }
            void operator() (const std::shared_ptr<ExpressionPredicate>& exprPred) const { std::visit(*this, exprPred->getExpression()); }
            void operator() (const std::shared_ptr<ComparisonPredicate>& compPred) const {
                _comparisons.push_back(compPred);
                std::visit(*this, compPred->getExpression1());
                std::visit(*this, compPred->getExpression2());
            }
            void operator() (const std::shared_ptr<NotPredicate>& notPred) const { std::visit(*this, notPred->getPredicate()); }
            void operator() (const std::shared_ptr<OrPredicate>& orPred) const { std::visit(*this, orPred->getPredicate1()); std::visit(*this, orPred->getPredicate2()); }
            void operator() (const std::shared_ptr<AndPredicate>& andPred) const { std::visit(*this, andPred->getPredicate1()); std::visit(*this, andPred->getPredicate2()); }

            void operator() (const Value& val) const { }
            void operator() (const Predicate& pred) const { std::visit(*this, pred); }
            void operator() (const std::shared_ptr<VariableExpression>& varExpr) const { std::visit(*this, varExpr->getVariableExpression()); }
            void operator() (const std::shared_ptr<UnaryExpression>& unaryExpr) const { std::visit(*this, unaryExpr->getExpression()); }
            void operator() (const std::shared_ptr<BinaryExpression>& binaryExpr) const { std::visit(*this, binaryExpr->getExpression1()); std::visit(*this, binaryExpr->getExpression2()); }
            void operator() (const std::shared_ptr<TertiaryExpression>& tertiaryExpr) const { std::visit(*this, tertiaryExpr->getExpression1()); std::visit(*this, tertiaryExpr->getExpression2()); std::visit(*this, tertiaryExpr->getExpression3()); }
            void operator() (const std::shared_ptr<InterpolateExpression>& interpExpr) const {
                std::visit(*this, interpExpr->getTimeExpression());
                for (const Expression& keyFrame : interpExpr->getKeyFrames()) {
                    std::visit(*this, keyFrame);
                }
            }
            void operator() (const std::shared_ptr<TransformExpression>& transExpr) const {
                for (const Expression& subExpr : transExpr->getSubExpressions()) {
                    std::visit(*this, subExpr);
                }
            }
            void operator() (const std::shared_ptr<FunctionExpression>& funcExpr) const {
                for (const Expression& subExpr : funcExpr->getExpressions()) {
                    std::visit(*this, subExpr);
                }
            }

        private:
            std::vector<std::shared_ptr<ComparisonPredicate>>& _comparisons;
        };

        std::vector<std::string> collectVariableNames(const Expression& expr) {
            std::vector<std::string> names;
            std::visit(ExpressionVariableVisitor([&names](const std::shared_ptr<VariableExpression>& varExpr) {
                if (auto val = std::get_if<Value>(&varExpr->getVariableExpression())) {
                    names.push_back(ValueConverter<std::string>::convert(*val));
                }
                else {
                    names.push_back(std::string()); // computed name: could be any variable
                }
            }), expr);
            return names;
        }

        std::size_t countNutiVariable(const Expression& expr, const std::string& name) {
            std::vector<std::string> names = collectVariableNames(expr);
            return std::count(names.begin(), names.end(), "nuti::" + name);
        }

        bool isNutiVariable(const Expression& expr, const std::string& name) {
            if (auto varExpr = std::get_if<std::shared_ptr<VariableExpression>>(&expr)) {
                if (auto val = std::get_if<Value>(&(*varExpr)->getVariableExpression())) {
                    return ValueConverter<std::string>::convert(*val) == "nuti::" + name;
                }
            }
            return false;
        }

        // The field expression the parameter is compared with, when EVERY read of it in this
        // expression is an '=' against something the feature alone answers. Nothing otherwise: a
        // read the fold cannot account for would be frozen at the value the decode happened to use.
        std::optional<Expression> resolveFieldExpression(const Expression& expr, const std::string& name) {
            std::size_t reads = countNutiVariable(expr, name);
            std::vector<std::shared_ptr<ComparisonPredicate>> comparisons;
            std::visit(ComparisonCollector(comparisons), expr);

            std::optional<Expression> fieldExpr;
            std::size_t foldedReads = 0;
            for (const std::shared_ptr<ComparisonPredicate>& compPred : comparisons) {
                if (countNutiVariable(Expression(Predicate(compPred)), name) == 0) {
                    continue;
                }
                if (compPred->getOp() != ComparisonPredicate::Op::EQ) {
                    return std::optional<Expression>();
                }
                const Expression* field = nullptr;
                if (isNutiVariable(compPred->getExpression1(), name)) {
                    field = &compPred->getExpression2();
                }
                else if (isNutiVariable(compPred->getExpression2(), name)) {
                    field = &compPred->getExpression1();
                }
                else {
                    return std::optional<Expression>(); // the parameter is buried in a larger expression
                }
                for (const std::string& varName : collectVariableNames(*field)) {
                    // The hash the feature keeps has to be fixed once the tile is decoded
                    if (varName.empty() || ExpressionContext::isNutiVariable(varName) || ExpressionContext::isViewStateVariable(varName)) {
                        return std::optional<Expression>();
                    }
                }
                if (fieldExpr && !std::visit(ExpressionDeepEqualsChecker(), *fieldExpr, *field)) {
                    return std::optional<Expression>(); // one parameter, one field: one hash per feature
                }
                fieldExpr = *field;
                foldedReads++;
            }
            if (!fieldExpr || foldedReads != reads) {
                return std::optional<Expression>();
            }
            return fieldExpr;
        }

        struct NutiParameterCollector {
            explicit NutiParameterCollector(std::set<std::string>& names, bool& computedName) : _names(names), _computedName(computedName) { }

            void operator() (const std::shared_ptr<VariableExpression>& varExpr) const {
                if (auto val = std::get_if<Value>(&varExpr->getVariableExpression())) {
                    std::string name = ValueConverter<std::string>::convert(*val);
                    if (ExpressionContext::isNutiVariable(name)) {
                        _names.insert(name.substr(6));
                    }
                }
                else {
                    _computedName = true; // the variable name is itself an expression: any parameter could be read here
                }
            }

        private:
            std::set<std::string>& _names;
            bool& _computedName;
        };
    }

    std::set<std::string> resolveLiveNutiParameters(const Map& map) {
        std::set<std::string> liveParameters, bakedParameters;
        bool computedName = false;

        for (const std::shared_ptr<Style>& style : map.getStyles()) {
            for (const std::shared_ptr<const Rule>& rule : style->getRules()) {
                // A parameter that selects rules decides what the tile contains at all
                if (const std::shared_ptr<const Filter>& filter = rule->getFilter()) {
                    if (filter->getPredicate()) {
                        std::visit(PredicateVariableVisitor(NutiParameterCollector(bakedParameters, computedName)), *filter->getPredicate());
                    }
                }

                for (const std::shared_ptr<const Symbolizer>& symbolizer : rule->getSymbolizers()) {
                    for (const std::string& propertyName : symbolizer->getPropertyNames()) {
                        const Property* property = symbolizer->getProperty(propertyName);
                        if (!property) {
                            continue;
                        }
                        bool live = property->isLiveCapable() && !property->isBakedAtDecode();
                        std::visit(ExpressionVariableVisitor(NutiParameterCollector(live ? liveParameters : bakedParameters, computedName)), property->getExpression());
                    }
                }
            }
        }

        if (computedName) {
            return std::set<std::string>();
        }

        std::set<std::string> result;
        for (const std::string& parameter : liveParameters) {
            if (bakedParameters.find(parameter) == bakedParameters.end()) {
                result.insert(parameter);
            }
        }
        return result;
    }

    std::optional<SelectionParameter> resolveSelectionParameter(Map& map, const std::shared_ptr<Logger>& logger) {
        // The three properties of a line that become style SLOTS - a slot is refilled per frame, so
        // both branches of the fold share the same vertices. Anything else a fold touched would have
        // to be tesselated twice, which is the decode this is here to avoid.
        static const std::set<std::string> FOLDABLE_PROPERTIES = { "stroke", "stroke-opacity", "stroke-width" };

        // Opt-in: a style that asks for nothing is not walked at all
        std::set<std::string> declaredParameters;
        for (const std::pair<const std::string, NutiParameter>& nutiParameter : map.getNutiParameterMap()) {
            if (nutiParameter.second.selectsFeatures()) {
                declaredParameters.insert(nutiParameter.first);
            }
        }
        if (declaredParameters.empty()) {
            return std::optional<SelectionParameter>();
        }

        std::map<std::string, std::string> refusals; // why a declared parameter cannot be folded
        std::map<std::string, Expression> fieldExpressions;
        std::map<std::string, std::vector<Property*>> foldableProperties;
        auto refuse = [&refusals, &declaredParameters](const std::set<std::string>& parameters, const std::string& reason) {
            for (const std::string& parameter : parameters) {
                if (declaredParameters.count(parameter) > 0) {
                    refusals.emplace(parameter, reason);
                }
            }
        };

        for (const std::shared_ptr<Style>& style : map.getStyles()) {
            for (const std::shared_ptr<const Rule>& rule : style->getRules()) {
                // A parameter that selects rules decides what the tile contains at all
                if (const std::shared_ptr<const Filter>& filter = rule->getFilter()) {
                    if (filter->getPredicate()) {
                        std::set<std::string> filterParameters;
                        bool computedName = false;
                        std::visit(PredicateVariableVisitor(NutiParameterCollector(filterParameters, computedName)), *filter->getPredicate());
                        refuse(filterParameters, "it is read by a rule filter, which decides whether the geometry exists at all");
                        if (computedName) {
                            refuse(declaredParameters, "a rule filter builds a variable name at runtime");
                        }
                    }
                }

                for (const std::shared_ptr<const Symbolizer>& symbolizer : rule->getSymbolizers()) {
                    bool lineSymbolizer = dynamic_cast<const LineSymbolizer*>(symbolizer.get()) != nullptr;
                    const Property* dashArray = lineSymbolizer ? symbolizer->getProperty("stroke-dasharray") : nullptr;
                    bool dashed = dashArray && dashArray->isDefined();

                    for (const std::string& propertyName : symbolizer->getPropertyNames()) {
                        const Property* property = symbolizer->getProperty(propertyName);
                        if (!property) {
                            continue;
                        }

                        std::set<std::string> parameters;
                        for (const std::string& varName : collectVariableNames(property->getExpression())) {
                            if (varName.empty()) {
                                refuse(declaredParameters, "the style builds a variable name at runtime");
                            }
                            else if (ExpressionContext::isNutiVariable(varName)) {
                                parameters.insert(varName.substr(6));
                            }
                        }
                        if (parameters.empty()) {
                            continue;
                        }

                        if (!lineSymbolizer || FOLDABLE_PROPERTIES.count(propertyName) == 0) {
                            refuse(parameters, "it is read by " + propertyName + ", which is not the colour, opacity or width of a line");
                            continue;
                        }
                        if (parameters.size() != 1) {
                            refuse(parameters, "it shares " + propertyName + " with another style parameter");
                            continue;
                        }
                        // The dash raster is sized by the line width, so a selected width would give
                        // the two branches different texture coordinates - different vertices.
                        if (dashed && propertyName == "stroke-width") {
                            refuse(parameters, "it sets the width of a DASHED line, whose dash raster is sized by that width");
                            continue;
                        }

                        const std::string& parameter = *parameters.begin();
                        std::optional<Expression> fieldExpr = resolveFieldExpression(property->getExpression(), parameter);
                        if (!fieldExpr) {
                            refuse(parameters, "it is not written as [nuti::" + parameter + "] = <expression of feature fields> in " + propertyName);
                            continue;
                        }
                        auto it = fieldExpressions.find(parameter);
                        if (it == fieldExpressions.end()) {
                            fieldExpressions.emplace(parameter, *fieldExpr);
                        }
                        else if (!std::visit(ExpressionDeepEqualsChecker(), it->second, *fieldExpr)) {
                            refuse(parameters, "it is compared with a different field expression in " + propertyName);
                            continue;
                        }
                        // The map is being finalised right after it was compiled, and the flag is
                        // about the property rather than about any one tile
                        foldableProperties[parameter].push_back(const_cast<Property*>(property));
                    }
                }
            }
        }

        std::optional<SelectionParameter> selectionParameter;
        for (const std::string& parameter : declaredParameters) {
            auto refusalIt = refusals.find(parameter);
            if (refusalIt == refusals.end() && fieldExpressions.find(parameter) == fieldExpressions.end()) {
                refusalIt = refusals.emplace(parameter, "no line colour, opacity or width reads it").first;
            }
            if (refusalIt != refusals.end()) {
                logger->write(Logger::Severity::WARNING, "Style parameter '" + parameter + "' declares itself as selecting, but " + refusalIt->second + " - setting it will decode the tiles again");
                continue;
            }
            if (selectionParameter) {
                // A decoded tile carries ONE selection state
                logger->write(Logger::Severity::WARNING, "More than one style parameter declares itself as selecting ('" + selectionParameter->name + "', '" + parameter + "') - none of them can repaint");
                return std::optional<SelectionParameter>();
            }
            selectionParameter = SelectionParameter { parameter, fieldExpressions.find(parameter)->second };
        }
        if (selectionParameter) {
            for (Property* property : foldableProperties[selectionParameter->name]) {
                property->setSelectionFoldable(true);
            }
        }
        return selectionParameter;
    }
}
