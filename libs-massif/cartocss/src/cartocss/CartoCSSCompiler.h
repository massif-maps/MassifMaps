/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_CARTOCSSCOMPILER_H_
#define _CARTO_CARTOCSS_CARTOCSSCOMPILER_H_

#include "Expression.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "StyleSheet.h"
#include "PropertySets.h"

#include <tuple>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <utility>

namespace carto { namespace css {
    class CartoCSSCompiler final {
    public:
        CartoCSSCompiler() = default;

        const ExpressionContext& getContext() const { return _context; }
        void setContext(const ExpressionContext& context) { _context = context; }

        bool isIgnoreLayerPredicates() const { return _ignoreLayerPredicates; }
        void setIgnoreLayerPredicates(bool ignoreLayerPredicates) { _ignoreLayerPredicates = ignoreLayerPredicates; }

        void compileMap(const StyleSheet& styleSheet, std::map<std::string, Value>& mapProperties) const;
        void compileLayer(const StyleSheet& styleSheet, const std::string& layerName, int minZoom, int maxZoom, std::map<std::pair<int, int>, std::list<AttachmentPropertySets>>& layerZoomAttachments) const;
        
    private:
        struct FilteredProperty {
            std::size_t property;
            std::vector<std::size_t> filters;

            bool operator == (const FilteredProperty& other) const {
                return property == other.property && filters == other.filters;
            }

            bool operator != (const FilteredProperty& other) const {
                return !(*this == other);
            }
        };

        struct FilteredPropertyList {
            std::string attachment;
            std::vector<FilteredProperty> properties;

            bool operator == (const FilteredPropertyList& other) const {
                return attachment == other.attachment && properties == other.properties;
            }

            bool operator != (const FilteredPropertyList& other) const {
                return !(*this == other);
            }
        };

        struct FilteredPropertySet {
            std::vector<std::size_t> filters;
            std::vector<std::size_t> properties;

            bool operator == (const FilteredPropertySet& other) const {
                return filters == other.filters && properties == other.properties;
            }

            bool operator != (const FilteredPropertySet& other) const {
                return !(*this == other);
            }
        };

        struct FilteredPropertyState {
            FilteredPropertyState() = default;

            std::shared_ptr<const Predicate> getPredicate(std::size_t predicate) const {
                return _predicates.at(predicate);
            }

            std::size_t insertPredicate(const Predicate& pred) {
                auto it = std::find_if(_predicates.begin(), _predicates.end(), [&pred](const std::shared_ptr<const Predicate>& otherPred) {
                    return pred == *otherPred;
                });
                if (it == _predicates.end()) {
                    it = _predicates.insert(it, std::make_shared<Predicate>(pred));
                    std::size_t i = it - _predicates.begin();
                    _predicateContains.emplace_back();
                    _predicateIntersects.emplace_back();
                    for (std::size_t j = 0; j < _predicates.size() - 1; j++) {
                        _predicateContains[i].push_back(std::visit(PredicateContainsChecker(), *_predicates[i], *_predicates[j]));
                        _predicateContains[j].push_back(std::visit(PredicateContainsChecker(), *_predicates[j], *_predicates[i]));
                        _predicateIntersects[i].push_back(std::visit(PredicateIntersectsChecker(), *_predicates[i], *_predicates[j]));
                        _predicateIntersects[j].push_back(std::visit(PredicateIntersectsChecker(), *_predicates[j], *_predicates[i]));
                    }
                    _predicateContains[i].push_back(true);
                    _predicateIntersects[i].push_back(true);
                }
                return it - _predicates.begin();
            }

            std::shared_ptr<const Property> getProperty(std::size_t property) const {
                return _properties.at(property);
            }

            std::size_t insertProperty(const Property& prop) {
                auto it = std::find_if(_properties.begin(), _properties.end(), [&prop](const std::shared_ptr<const Property>& otherProp) {
                    return prop == *otherProp;
                });
                if (it == _properties.end()) {
                    it = _properties.insert(it, std::make_shared<Property>(prop));
                }
                return it - _properties.begin();
            }

            std::shared_ptr<const Property> findPropertySetProperty(const FilteredPropertySet& propertySet, const std::string& field) const {
                auto it = std::find_if(propertySet.properties.begin(), propertySet.properties.end(), [&field, this](std::size_t existingProperty) {
                    return _properties[existingProperty]->getField() == field;
                });
                return it != propertySet.properties.end() ? _properties[*it] : std::shared_ptr<const Property>();
            }

            bool mergePropertySetProperty(FilteredPropertySet& existingPropertySet, const FilteredProperty& property) const {
                for (std::size_t filter : property.filters) {
                    for (std::size_t existingFilter : existingPropertySet.filters) {
                        if (!_predicateIntersects[filter][existingFilter]) {
                            return false;
                        }
                    }

                    bool insert = true;
                    for (std::size_t& existingFilter : existingPropertySet.filters) {
                        if (_predicateContains[filter][existingFilter]) {
                            insert = false;
                            break;
                        }
                        if (_predicateContains[existingFilter][filter]) {
                            existingFilter = filter;
                            insert = false;
                            break;
                        }
                    }
                    if (insert) {
                        existingPropertySet.filters.push_back(filter);
                    }
                }

                auto it = std::find_if(existingPropertySet.properties.begin(), existingPropertySet.properties.end(), [&property, this](std::size_t existingProperty) {
                    return _properties[existingProperty]->getField() == _properties[property.property]->getField();
                });
                if (it != existingPropertySet.properties.end()) {
                    *it = property.property;
                } else {
                    existingPropertySet.properties.insert(it, property.property);
                }
                return true;
            }

            bool testPropertySetFilterCover(const FilteredPropertySet& existingPropertySet, const FilteredPropertySet& propertySet) const {
                return std::all_of(existingPropertySet.filters.begin(), existingPropertySet.filters.end(), [&, this](std::size_t existingFilter) {
                    return std::any_of(propertySet.filters.begin(), propertySet.filters.end(), [existingFilter, this](std::size_t filter) {
                        return _predicateContains[existingFilter][filter];
                    });
                });
            }

        private:
            std::vector<std::shared_ptr<const Predicate>> _predicates;
            std::vector<std::shared_ptr<const Property>> _properties;

            std::vector<std::vector<boost::tribool>> _predicateContains;
            std::vector<std::vector<boost::tribool>> _predicateIntersects;
        };

        void buildPropertyLists(const StyleSheet& styleSheet, PredicateContext& context, FilteredPropertyState& state, std::list<FilteredPropertyList>& propertyLists) const;
        void buildPropertyList(const RuleSet& ruleSet, const PredicateContext& context, const std::string& existingAttachment, const std::vector<std::size_t>& existingFilters, FilteredPropertyState& state, std::list<FilteredPropertyList>& propertyLists) const;
        void buildLayerAttachment(const FilteredPropertyList& propertyList, const FilteredPropertyState& state, std::list<AttachmentPropertySets>& layerAttachments) const;
        
        static Property::RuleSpecificity calculateRuleSpecificity(const std::vector<size_t>& filters, const FilteredPropertyState& state, int order);

        ExpressionContext _context;
        bool _ignoreLayerPredicates = false;
    };
} }

#endif
