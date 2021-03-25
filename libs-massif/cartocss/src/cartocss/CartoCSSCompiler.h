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

        void setContext(const ExpressionContext& context) { _context = context; }
        void setIgnoreLayerPredicates(bool ignoreLayerPredicates) { _ignoreLayerPredicates = ignoreLayerPredicates; }

        void compileMap(const StyleSheet& styleSheet, std::map<std::string, Value>& mapProperties) const;
        void compileLayer(const StyleSheet& styleSheet, const std::string& layerName, int minZoom, int maxZoom, std::map<std::pair<int, int>, std::list<AttachmentPropertySets>>& layerZoomAttachments) const;
        
    private:
        struct FilteredProperty {
            Property property;
            std::vector<std::shared_ptr<Predicate>> filters;
        };

        struct FilteredPropertyList {
            std::string attachment;
            std::list<FilteredProperty> properties;
        };

        void buildPropertyLists(const StyleSheet& styleSheet, PredicateContext& context, std::list<std::pair<Predicate, std::shared_ptr<Predicate>>>& predList, std::list<FilteredPropertyList>& propertyLists) const;
        void buildPropertyList(const RuleSet& ruleSet, const PredicateContext& context, const std::string& attachment, const std::vector<std::shared_ptr<Predicate>>& filters, std::list<std::pair<Predicate, std::shared_ptr<Predicate>>>& predList, std::list<FilteredPropertyList>& propertyLists) const;
        void buildLayerAttachment(const FilteredPropertyList& propertyList, std::list<AttachmentPropertySets>& layerAttachments) const;
        
        static std::shared_ptr<Predicate> getPredicatePtr(const Predicate& pred, std::list<std::pair<Predicate, std::shared_ptr<Predicate>>>& predList);

        static Property::RuleSpecificity calculateRuleSpecificity(const std::vector<std::shared_ptr<Predicate>>& predicates, int order);

        ExpressionContext _context;
        bool _ignoreLayerPredicates = false;
    };
} }

#endif
