/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_CARTOCSSCOMPILER_H_
#define _CARTO_CARTOCSS_CARTOCSSCOMPILER_H_

#include "Expression.h"
#include "Predicate.h"
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

        struct FilteredPropertyListState {
            std::vector<std::shared_ptr<const Predicate>> predicates;
            std::vector<std::shared_ptr<const Property>> properties;

            std::size_t insertPredicate(const Predicate& pred) {
                auto it = std::find_if(predicates.begin(), predicates.end(), [&pred](const std::shared_ptr<const Predicate>& otherPred) {
                    return pred == *otherPred;
                });
                if (it == predicates.end()) {
                    it = predicates.insert(it, std::make_shared<Predicate>(pred));
                }
                return it - predicates.begin();
            }

            std::size_t insertProperty(const Property& prop) {
                auto it = std::find_if(properties.begin(), properties.end(), [&prop](const std::shared_ptr<const Property>& otherProp) {
                    return prop == *otherProp;
                });
                if (it == properties.end()) {
                    it = properties.insert(it, std::make_shared<Property>(prop));
                }
                return it - properties.begin();
            }
        };

        void buildPropertyLists(const StyleSheet& styleSheet, PredicateContext& context, FilteredPropertyListState& state, std::list<FilteredPropertyList>& propertyLists) const;
        void buildPropertyList(const RuleSet& ruleSet, const PredicateContext& context, const std::string& existingAttachment, const std::vector<std::size_t>& existingFilters, FilteredPropertyListState& state, std::list<FilteredPropertyList>& propertyLists) const;
        void buildLayerAttachment(const FilteredPropertyList& propertyList, const FilteredPropertyListState& state, std::list<AttachmentPropertySets>& layerAttachments) const;
        
        static Property::RuleSpecificity calculateRuleSpecificity(const std::vector<size_t>& filters, const FilteredPropertyListState& state, int order);

        ExpressionContext _context;
        bool _ignoreLayerPredicates = false;
    };
} }

#endif
