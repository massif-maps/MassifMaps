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
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <unordered_map>
#include <utility>

namespace carto::css {
    class CartoCSSCompiler final {
    public:
        CartoCSSCompiler() = default;

        const ExpressionContext& getContext() const { return _context; }
        void setContext(const ExpressionContext& context) { _context = context; }

        bool isIgnoreLayerPredicates() const { return _ignoreLayerPredicates; }
        void setIgnoreLayerPredicates(bool ignoreLayerPredicates) { _ignoreLayerPredicates = ignoreLayerPredicates; }

        void compileMap(const StyleSheet& styleSheet, std::map<std::string, Expression>& mapProperties, std::map<std::string, Value>& constantFieldMap) const;
        void compileLayer(const StyleSheet& styleSheet, const std::string& layerName, int minZoom, int maxZoom, std::map<std::pair<int, int>, std::list<AttachmentPropertySets>>& layerZoomAttachments, std::map<std::string, Value>& constantFieldMap) const;
        
    private:
        struct FilteredProperty {
            std::size_t property = 0;
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

        // A layer stays far below both: ~50 property fields and ~80 predicates for the biggest layer
        // of a full OSM style. Anything above falls back to the scans these masks replace.
        static constexpr std::size_t FIELD_MASK_BITS = 256;
        static constexpr std::size_t PREDICATE_MASK_BITS = 256;

        using PredicateMask = std::array<std::uint64_t, PREDICATE_MASK_BITS / 64>;

        struct FilteredPropertySet {
            std::vector<std::size_t> filters;
            std::vector<std::size_t> properties;
            std::uint64_t fieldMask[FIELD_MASK_BITS / 64] = { 0, 0, 0, 0 }; // which fields 'properties' sets
            PredicateMask filterMask = {}; // which predicates 'filters' holds

            bool operator == (const FilteredPropertySet& other) const {
                return filters == other.filters && properties == other.properties;
            }

            bool operator != (const FilteredPropertySet& other) const {
                return !(*this == other);
            }
        };

        struct FilteredPropertyState {
            FilteredPropertyState() = default;

            std::size_t getPredicateCount() const { return _predicates.size(); }

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
                    _predicateContainsMasks.emplace_back();
                    _predicateDisjointMasks.emplace_back();
                    for (std::size_t j = 0; j < _predicates.size() - 1; j++) {
                        boost::tribool containsIJ = std::visit(PredicateContainsChecker(), *_predicates[i], *_predicates[j]);
                        boost::tribool containsJI = std::visit(PredicateContainsChecker(), *_predicates[j], *_predicates[i]);
                        boost::tribool intersectsIJ = std::visit(PredicateIntersectsChecker(), *_predicates[i], *_predicates[j]);
                        boost::tribool intersectsJI = std::visit(PredicateIntersectsChecker(), *_predicates[j], *_predicates[i]);
                        _predicateContains[i].push_back(containsIJ);
                        _predicateContains[j].push_back(containsJI);
                        _predicateIntersects[i].push_back(intersectsIJ);
                        _predicateIntersects[j].push_back(intersectsJI);
                        setPredicateMaskBit(_predicateContainsMasks[i], j, bool(containsIJ));
                        setPredicateMaskBit(_predicateContainsMasks[j], i, bool(containsJI));
                        setPredicateMaskBit(_predicateDisjointMasks[i], j, bool(!intersectsIJ));
                        setPredicateMaskBit(_predicateDisjointMasks[j], i, bool(!intersectsJI));
                    }
                    _predicateContains[i].push_back(true);
                    _predicateIntersects[i].push_back(true);
                    setPredicateMaskBit(_predicateContainsMasks[i], i, true);
                }
                return it - _predicates.begin();
            }

            std::shared_ptr<const Property> getProperty(std::size_t property) const {
                return _properties.at(property);
            }

            // Reference accessors for the hot paths (the sort comparator, buildLayerAttachment):
            // handing back a shared_ptr there costs an atomic pair per property visited.
            const Property& getPropertyRef(std::size_t property) const {
                return *_properties[property];
            }

            // Two properties can only collide when they set the same field, and comparing the
            // field strings is the innermost operation of buildLayerAttachment - intern them.
            std::size_t getPropertyFieldId(std::size_t property) const {
                return _propertyFieldIds[property];
            }

            std::size_t insertProperty(const Property& prop) {
                // Only properties of the same field can compare equal, so the scan goes over that
                // field's bucket instead of everything inserted so far - comparing two properties
                // means a deep expression comparison, and a big layer inserts hundreds of them.
                std::size_t fieldId = _fieldIds.emplace(prop.getField(), _fieldIds.size()).first->second;
                std::vector<std::size_t>& fieldProperties = _fieldPropertyIndex[fieldId];
                auto it = std::find_if(fieldProperties.begin(), fieldProperties.end(), [&prop, this](std::size_t otherProperty) {
                    // Same bucket means the same field, so compare the rest: the specificity first,
                    // which rules most candidates out before the deep expression comparison.
                    const Property& otherProp = *_properties[otherProperty];
                    return prop.getSpecificity() == otherProp.getSpecificity() && std::visit(ExpressionDeepEqualsChecker(), prop.getExpression(), otherProp.getExpression());
                });
                if (it != fieldProperties.end()) {
                    return *it;
                }
                _properties.push_back(std::make_shared<Property>(prop));
                _propertyFieldIds.push_back(fieldId);
                fieldProperties.push_back(_properties.size() - 1);
                return _properties.size() - 1;
            }

            // Exact membership test - every (property, property set) pair asks it, and it replaces
            // a scan over the whole property list of the set. Fields past the mask (no real style
            // gets there) fall back to that scan.
            bool propertySetHasField(const FilteredPropertySet& propertySet, std::size_t fieldId) const {
                if (fieldId >= FIELD_MASK_BITS) {
                    return findPropertySetProperty(propertySet, fieldId) != nullptr;
                }
                return (propertySet.fieldMask[fieldId / 64] & fieldMaskBit(fieldId)) != 0;
            }

            const Property* findPropertySetProperty(const FilteredPropertySet& propertySet, std::size_t fieldId) const {
                auto it = std::find_if(propertySet.properties.begin(), propertySet.properties.end(), [fieldId, this](std::size_t existingProperty) {
                    return _propertyFieldIds[existingProperty] == fieldId;
                });
                return it != propertySet.properties.end() ? _properties[*it].get() : nullptr;
            }

            // The first thing mergePropertySetProperty does, without the property set copy the merge
            // needs. Most pairs are rejected here, so the copy is worth avoiding. One mask test per
            // filter replaces the walk over the property set's own filters.
            bool canMergePropertySetProperty(const FilteredPropertySet& propertySet, const FilteredProperty& property) const {
                if (_predicateMasksOverflowed) {
                    for (std::size_t filter : property.filters) {
                        for (std::size_t existingFilter : propertySet.filters) {
                            if (!_predicateIntersects[filter][existingFilter]) {
                                return false;
                            }
                        }
                    }
                    return true;
                }
                for (std::size_t filter : property.filters) {
                    if (predicateMasksIntersect(_predicateDisjointMasks[filter], propertySet.filterMask)) {
                        return false;
                    }
                }
                return true;
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

                std::size_t fieldId = _propertyFieldIds[property.property];
                auto it = existingPropertySet.properties.end();
                if (propertySetHasField(existingPropertySet, fieldId)) {
                    it = std::find_if(existingPropertySet.properties.begin(), existingPropertySet.properties.end(), [fieldId, this](std::size_t existingProperty) {
                        return _propertyFieldIds[existingProperty] == fieldId;
                    });
                }
                if (it != existingPropertySet.properties.end()) {
                    *it = property.property;
                } else {
                    existingPropertySet.properties.push_back(property.property);
                }
                if (fieldId < FIELD_MASK_BITS) {
                    existingPropertySet.fieldMask[fieldId / 64] |= fieldMaskBit(fieldId);
                }
                rebuildFilterMask(existingPropertySet);
                return true;
            }

            bool testPropertySetFilterCover(const FilteredPropertySet& existingPropertySet, const FilteredPropertySet& propertySet) const {
                if (_predicateMasksOverflowed) {
                    return std::all_of(existingPropertySet.filters.begin(), existingPropertySet.filters.end(), [&, this](std::size_t existingFilter) {
                        return std::any_of(propertySet.filters.begin(), propertySet.filters.end(), [existingFilter, this](std::size_t filter) {
                            return _predicateContains[existingFilter][filter];
                        });
                    });
                }
                return std::all_of(existingPropertySet.filters.begin(), existingPropertySet.filters.end(), [&, this](std::size_t existingFilter) {
                    return predicateMasksIntersect(_predicateContainsMasks[existingFilter], propertySet.filterMask);
                });
            }

        private:
            static std::uint64_t fieldMaskBit(std::size_t fieldId) {
                return std::uint64_t(1) << (fieldId % 64);
            }

            void setPredicateMaskBit(PredicateMask& mask, std::size_t predicate, bool value) const {
                if (predicate >= PREDICATE_MASK_BITS) {
                    _predicateMasksOverflowed = true;
                    return;
                }
                if (value) {
                    mask[predicate / 64] |= std::uint64_t(1) << (predicate % 64);
                }
            }

            static bool predicateMasksIntersect(const PredicateMask& mask1, const PredicateMask& mask2) {
                for (std::size_t i = 0; i < mask1.size(); i++) {
                    if (mask1[i] & mask2[i]) {
                        return true;
                    }
                }
                return false;
            }

            // The filters of a property set are rewritten in place by mergePropertySetProperty, so
            // the mask is rebuilt from them rather than tracked through every case.
            void rebuildFilterMask(FilteredPropertySet& propertySet) const {
                propertySet.filterMask = PredicateMask {};
                for (std::size_t filter : propertySet.filters) {
                    setPredicateMaskBit(propertySet.filterMask, filter, true);
                }
            }

            std::vector<std::shared_ptr<const Predicate>> _predicates;
            std::vector<std::shared_ptr<const Property>> _properties;
            std::vector<std::size_t> _propertyFieldIds; // property -> interned field id
            std::unordered_map<std::string, std::size_t> _fieldIds;
            std::unordered_map<std::size_t, std::vector<std::size_t>> _fieldPropertyIndex; // field id -> properties

            std::vector<std::vector<boost::tribool>> _predicateContains;
            std::vector<std::vector<boost::tribool>> _predicateIntersects;
            std::vector<PredicateMask> _predicateContainsMasks; // definitely-contains rows of _predicateContains
            std::vector<PredicateMask> _predicateDisjointMasks; // definitely-does-not-intersect rows
            mutable bool _predicateMasksOverflowed = false; // set from the const merge path too
        };

        void buildPropertyLists(const StyleSheet& styleSheet, PredicateContext& context, FilteredPropertyState& state, std::list<FilteredPropertyList>& propertyLists) const;
        void buildPropertyList(const RuleSet& ruleSet, const PredicateContext& context, const std::string& existingAttachment, const std::vector<std::size_t>& existingFilters, FilteredPropertyState& state, std::list<FilteredPropertyList>& propertyLists) const;
        void buildLayerAttachment(const FilteredPropertyList& propertyList, const FilteredPropertyState& state, std::list<AttachmentPropertySets>& layerAttachments) const;
        
        static Property::RuleSpecificity calculateRuleSpecificity(const std::vector<size_t>& filters, const FilteredPropertyState& state, int order);

        ExpressionContext _context;
        bool _ignoreLayerPredicates = false;
    };
}

#endif
