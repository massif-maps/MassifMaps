#include "Rule.h"
#include "Expression.h"
#include "Filter.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "Rule.h"
#include "Symbolizer.h"

#include <algorithm>

namespace carto::mvt {
    Rule::Rule(std::string name, int minZoom, int maxZoom, std::shared_ptr<const Filter> filter, std::vector<std::shared_ptr<const Symbolizer>> symbolizers) : _name(std::move(name)), _minZoom(minZoom), _maxZoom(maxZoom), _filter(std::move(filter)), _symbolizers(std::move(symbolizers)) {
    }

    void Rule::calculateReferencedFields() const {
        struct FieldExtractor {
            explicit FieldExtractor(std::set<std::string>& fields) : _fields(fields) { }

            void operator() (const std::shared_ptr<VariableExpression>& varExpr) {
                if (auto val = std::get_if<Value>(&varExpr->getVariableExpression())) {
                    _fields.insert(ValueConverter<std::string>::convert(*val));
                }
                else {
                    _fields.insert(std::string()); // generic expression, insert special marker that any fields can be used
                }
            }

        private:
            std::set<std::string>& _fields;
        };

        std::lock_guard<std::mutex> lock(_referencedFieldsMutex);
        if (_referencedFieldsCalculated) {
            return;
        }

        _referencedFilterFields.clear();
        if (_filter) {
            if (_filter->getPredicate()) {
                std::visit(PredicateVariableVisitor(FieldExtractor(_referencedFilterFields)), *_filter->getPredicate());
            }
        }

        _referencedSymbolizerFields.clear();
        for (const std::shared_ptr<const Symbolizer>& symbolizer : _symbolizers) {
            for (const std::string& paramName : symbolizer->getParameterNames()) {
                if (auto param = symbolizer->getParameter(paramName)) {
                    std::visit(ExpressionVariableVisitor(FieldExtractor(_referencedSymbolizerFields)), param->getExpression());
                }
            }
        }

        _referencedFieldsCalculated = true;
    }
}
