#include "Style.h"
#include "Expression.h"
#include "Filter.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "Rule.h"
#include "Symbolizer.h"

#include <algorithm>

namespace carto { namespace mvt {
    Style::Style(std::string name, float opacity, std::string imageFilters, std::optional<vt::CompOp> compOp, FilterMode filterMode, std::vector<std::shared_ptr<const Rule>> rules) : _name(std::move(name)), _opacity(opacity), _imageFilters(std::move(imageFilters)), _compOp(std::move(compOp)), _filterMode(filterMode), _rules(std::move(rules)) {
        rebuildZoomRuleMap();
    }

    const std::vector<std::shared_ptr<const Rule>>& Style::getZoomRules(int zoom) const {
        static const std::vector<std::shared_ptr<const Rule>> emptyRules;
        auto it = _zoomRuleMap.find(zoom);
        if (it == _zoomRuleMap.end()) {
            return emptyRules;
        }
        return it->second;
    }

    void Style::optimizeRules() {
        if (_filterMode != FilterMode::FIRST) {
            return;
        }
        if (_rules.size() < 1) {
            return;
        }

        bool updated = false;
        
        // Try to merge consecutive rules R1 and R2 with R1.maxZoom=R2.minZoom assuming everything else is equal
        for (auto it = _rules.begin(); it + 1 != _rules.end(); ) {
            std::shared_ptr<const Rule> rule1 = *(it + 0);
            std::shared_ptr<const Rule> rule2 = *(it + 1);
            if (rule1->getMinZoom() > rule2->getMinZoom()) {
                std::swap(rule1, rule2);
            }
            std::optional<Predicate> pred1 = rule1->getFilter()->getPredicate();
            std::optional<Predicate> pred2 = rule2->getFilter()->getPredicate();
            bool samePred = (pred1 == pred2) || (pred1 && pred2 && std::visit(PredicateDeepEqualsChecker(), *pred1, *pred2));
            if (rule1->getMaxZoom() == rule2->getMinZoom() && rule1->getFilter()->getType() == rule2->getFilter()->getType() && samePred && rule1->getSymbolizers() == rule2->getSymbolizers()) {
                auto combinedRule = std::make_shared<Rule>("combined", rule1->getMinZoom(), rule2->getMaxZoom(), rule1->getFilter(), rule1->getSymbolizers());
                it = _rules.erase(it);
                *it = combinedRule;
                updated = true;
            } else {
                it++;
            }
        }

        // Try to merge consecutive rules R1 and R2 with different filter expressions but with everything else equal
        for (auto it = _rules.begin(); it + 1 != _rules.end(); ) {
            std::shared_ptr<const Rule> rule1 = *(it + 0);
            std::shared_ptr<const Rule> rule2 = *(it + 1);
            if (rule1->getMinZoom() == rule2->getMinZoom() && rule1->getMaxZoom() == rule2->getMaxZoom() && rule1->getFilter()->getType() == Filter::Type::FILTER && rule2->getFilter()->getType() == Filter::Type::FILTER && rule1->getSymbolizers() == rule2->getSymbolizers()) {
                auto combinedPred = buildOptimizedOrPredicate(rule1->getFilter()->getPredicate(), rule2->getFilter()->getPredicate());
                auto combinedFilter = std::make_shared<Filter>(Filter::Type::FILTER, combinedPred);
                auto combinedRule = std::make_shared<Rule>("combined", rule1->getMinZoom(), rule1->getMaxZoom(), combinedFilter, rule1->getSymbolizers());
                it = _rules.erase(it);
                *it = combinedRule;
                updated = true;
            } else {
                it++;
            }
        }

        if (updated) {
            rebuildZoomRuleMap();
        }
    }

    void Style::rebuildZoomRuleMap() {
        _zoomRuleMap.clear();
        for (const std::shared_ptr<const Rule>& rule : _rules) {
            for (int zoom = rule->getMinZoom(); zoom < rule->getMaxZoom(); zoom++) {
                _zoomRuleMap[zoom].push_back(rule);
            }
        }
    }

    std::optional<Predicate> Style::buildOptimizedOrPredicate(const std::optional<Predicate>& pred1, const std::optional<Predicate>& pred2) {
        // X or true = true
        if (!pred1 || !pred2) {
            return std::optional<Predicate>();
        }

        // X or X = X
        if (pred1 == pred2 || std::visit(PredicateDeepEqualsChecker(), *pred1, *pred2)) {
            return pred1;
        }

        // X or (X & Y) = X
        std::array<const Predicate*, 2> preds = {{ &*pred1, &*pred2 }};
        for (int i1 = 0; i1 < 2; i1++) {
            if (auto andPred2 = std::get_if<std::shared_ptr<AndPredicate>>(&*preds[i1 ^ 1])) {
                std::array<const Predicate*, 2> subPreds2 = {{ &(*andPred2)->getPredicate1(), &(*andPred2)->getPredicate2() }};
                for (int i2 = 0; i2 < 2; i2++) {
                    if (std::visit(PredicateDeepEqualsChecker(), *preds[i1], *subPreds2[i2])) {
                        return *preds[i1];
                    }
                }
            }
        }

        // (X & Y) or (X & Z) = X & (Y or Z)
        if (auto andPred1 = std::get_if<std::shared_ptr<AndPredicate>>(&*pred1)) {
            std::array<const Predicate*, 2> subPreds1 = {{ &(*andPred1)->getPredicate1(), &(*andPred1)->getPredicate2() }};
            if (auto andPred2 = std::get_if<std::shared_ptr<AndPredicate>>(&*pred2)) {
                std::array<const Predicate*, 2> subPreds2 = {{ &(*andPred2)->getPredicate1(), &(*andPred2)->getPredicate2() }};
                for (int i1 = 0; i1 < 2; i1++) {
                    for (int i2 = 0; i2 < 2; i2++) {
                        if (std::visit(PredicateDeepEqualsChecker(), *subPreds1[i1], *subPreds2[i2])) {
                            return std::make_shared<AndPredicate>(*subPreds1[i1], std::make_shared<OrPredicate>(*subPreds1[i1 ^ 1], *subPreds2[i2 ^ 1]));
                        }
                    }
                }
            }
        }

        // Just combine
        return std::make_shared<OrPredicate>(*pred1, *pred2);
    }
} }
