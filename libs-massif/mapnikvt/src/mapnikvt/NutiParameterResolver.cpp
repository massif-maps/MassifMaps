#include "NutiParameterResolver.h"
#include "Expression.h"
#include "ExpressionContext.h"
#include "ExpressionUtils.h"
#include "Filter.h"
#include "Map.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "Properties.h"
#include "Rule.h"
#include "Style.h"
#include "Symbolizer.h"
#include "ValueConverter.h"

namespace carto::mvt {
    namespace {
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
}
