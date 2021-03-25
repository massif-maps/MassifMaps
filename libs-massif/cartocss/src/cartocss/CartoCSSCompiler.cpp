#include "CartoCSSCompiler.h"
#include "ExpressionUtils.h"
#include "PredicateUtils.h"

#include <set>

namespace carto { namespace css {
    void CartoCSSCompiler::compileMap(const StyleSheet& styleSheet, std::map<std::string, Value>& mapProperties) const {
        // Build flat property lists
        std::map<std::string, Expression> variableMap;
        PredicateContext context;
        context.expressionContext = _context;
        context.expressionContext.variableMap = &variableMap;
        std::list<FilteredPropertyList> propertyLists;
        std::list<std::pair<Predicate, std::shared_ptr<Predicate>>> predList;
        buildPropertyLists(styleSheet, context, predList, propertyLists);

        // Gather map properties from property lists
        for (const FilteredPropertyList& propertyList : propertyLists) {
            if (propertyList.attachment.empty()) {
                for (const FilteredProperty& prop : propertyList.properties) {
                    if (prop.filters.empty()) {
                        Expression expr = std::visit(ExpressionEvaluator(context.expressionContext), prop.property.getExpression());
                        if (auto val = std::get_if<Value>(&expr)) {
                            mapProperties[prop.property.getField()] = *val;
                        }
                    }
                }
            }
        }
    }
    
    void CartoCSSCompiler::compileLayer(const StyleSheet& styleSheet, const std::string& layerName, int minZoom, int maxZoom, std::map<std::pair<int, int>, std::list<AttachmentPropertySets>>& layerZoomAttachments) const {
        // Build flat property lists
        std::map<std::string, Expression> variableMap;
        PredicateContext context;
        context.layerName = layerName;
        context.expressionContext = _context;
        context.expressionContext.variableMap = &variableMap;
        std::list<FilteredPropertyList> propertyLists;
        std::list<std::pair<Predicate, std::shared_ptr<Predicate>>> predList;
        buildPropertyLists(styleSheet, context, predList, propertyLists);

        // Sort the properties by decreasing specificity
        for (FilteredPropertyList& propertyList : propertyLists) {
            propertyList.properties.sort([](const FilteredProperty& prop1, const FilteredProperty& prop2) {
                return prop1.property.getSpecificity() > prop2.property.getSpecificity();
            });
        }

        // Build layer attachments by zoom
        int prevZoom = minZoom, zoom = minZoom;
        std::list<AttachmentPropertySets> prevLayerAttachments;
        for (; zoom < maxZoom; zoom++) {
            std::map<std::string, Value> predefinedFieldMap;
            context.expressionContext.predefinedFieldMap = &predefinedFieldMap;
            (*context.expressionContext.predefinedFieldMap)["zoom"] = Value(static_cast<long long>(zoom));
            PredicateEvaluator evaluator(context);

            // Sort and evaluate property list
            std::list<AttachmentPropertySets> layerAttachments;
            for (const FilteredPropertyList& propertyList : propertyLists) {
                // Build optimized property set list
                FilteredPropertyList optimizedPropertyList;
                optimizedPropertyList.attachment = propertyList.attachment;
                
                for (FilteredProperty prop : propertyList.properties) {
                    // Check if this property is reachable and remove always true conditions
                    bool unreachableProp = false;
                    for (auto it = prop.filters.begin(); it != prop.filters.end(); ) {
                        boost::tribool pred = std::visit(evaluator, **it);
                        if (!pred) {
                            unreachableProp = true;
                            break;
                        }
                        if (pred) {
                            it = prop.filters.erase(it);
                        }
                        else {
                            it++;
                        }
                    }

                    // Try to evaluate property expression, store the result as expression (even when constant)
                    if (!unreachableProp) {
                        prop.property.simplifyExpression(context.expressionContext);
                        optimizedPropertyList.properties.push_back(std::move(prop));
                    }
                }

                // Build attachments
                buildLayerAttachment(optimizedPropertyList, layerAttachments);
            }

            // Store the attachments
            if (layerAttachments != prevLayerAttachments) {
                if (zoom > prevZoom) {
                    layerZoomAttachments[std::make_pair(prevZoom, zoom)] = std::move(prevLayerAttachments);
                }
                prevLayerAttachments = std::move(layerAttachments);
                prevZoom = zoom;
            }
        }
        if (zoom > prevZoom) {
            layerZoomAttachments[std::make_pair(prevZoom, zoom)] = std::move(prevLayerAttachments);
        }
    }

    void CartoCSSCompiler::buildPropertyLists(const StyleSheet& styleSheet, PredicateContext& context, std::list<std::pair<Predicate, std::shared_ptr<Predicate>>>& predList, std::list<FilteredPropertyList>& propertyLists) const {
        for (const StyleSheet::Element& element : styleSheet.getElements()) {
            if (auto decl = std::get_if<VariableDeclaration>(&element)) {
                if (context.expressionContext.variableMap->find(decl->getVariable()) == context.expressionContext.variableMap->end()) {
                    (*context.expressionContext.variableMap)[decl->getVariable()] = decl->getExpression();
                }
            }
            else if (auto ruleSet = std::get_if<RuleSet>(&element)) {
                buildPropertyList(*ruleSet, context, "", std::vector<std::shared_ptr<Predicate>>(), predList, propertyLists);
            }
        }
    }
    
    void CartoCSSCompiler::buildPropertyList(const RuleSet& ruleSet, const PredicateContext& context, const std::string& attachment, const std::vector<std::shared_ptr<Predicate>>& filters, std::list<std::pair<Predicate, std::shared_ptr<Predicate>>>& predList, std::list<FilteredPropertyList>& propertyLists) const {
        // List of selectors to use
        const std::vector<Selector>* selectors = &ruleSet.getSelectors();
        if (selectors->empty() && !context.layerName.empty()) {
            static const std::vector<Selector> emptySelectorSet { Selector() };
            selectors = &emptySelectorSet;
        }
        
        // Process all selectors
        for (const Selector& selector : *selectors) {
            // Build filters for given selector
            std::vector<std::shared_ptr<Predicate>> selectorFilters(filters);
            for (const Predicate& pred : selector.getPredicates()) {
                if (std::holds_alternative<LayerPredicate>(pred)) {
                    if (_ignoreLayerPredicates) {
                        continue;
                    }
                }
                selectorFilters.push_back(getPredicatePtr(pred, predList));
            }
            
            // Check if the filter list is 'reachable'. Also, remove always true conditions from the filter
            bool unreachable = false;
            std::string selectorAttachment = attachment;
            std::vector<std::shared_ptr<Predicate>> optimizedSelectorFilters;
            for (auto it = selectorFilters.begin(); it != selectorFilters.end(); ) {
                boost::tribool pred = std::visit(PredicateEvaluator(context), **it);
                if (!pred) {
                    unreachable = true;
                    break;
                }
                if (auto attachmentPred = std::get_if<AttachmentPredicate>(&**it)) {
                    selectorAttachment += "::" + attachmentPred->getAttachment();
                    it = selectorFilters.erase(it);
                    continue;
                }
                if (boost::indeterminate(pred)) { // ignore always true filters
                    optimizedSelectorFilters.push_back(*it);
                }
                it++;
            }
            if (unreachable) {
                continue;
            }
            
            // Process block elements
            std::set<std::string> existingBlockFields;
            for (const Block::Element& element : ruleSet.getBlock().getElements()) {
                if (auto decl = std::get_if<PropertyDeclaration>(&element)) {
                    if (existingBlockFields.find(decl->getField()) != existingBlockFields.end()) {
                        continue;
                    }
                    existingBlockFields.insert(decl->getField());
                    
                    // Find property set list for current attachment
                    auto propertyListsIt = std::find_if(propertyLists.begin(), propertyLists.end(), [&](const FilteredPropertyList& propertyList) {
                        return propertyList.attachment == selectorAttachment;
                    });
                    if (propertyListsIt == propertyLists.end()) {
                        FilteredPropertyList propertyList;
                        propertyList.attachment = selectorAttachment;
                        propertyListsIt = propertyLists.insert(propertyListsIt, propertyList);
                    }
                    std::list<FilteredProperty>& properties = (*propertyListsIt).properties;
                    
                    // Add property
                    FilteredProperty prop;
                    prop.property = Property(decl->getField(), decl->getExpression(), calculateRuleSpecificity(selectorFilters, decl->getOrder()));
                    prop.filters = optimizedSelectorFilters;
                    properties.push_back(std::move(prop));
                }
                else if (auto subRuleSet = std::get_if<RuleSet>(&element)) {
                    // Recurse with subrule
                    buildPropertyList(*subRuleSet, context, selectorAttachment, selectorFilters, predList, propertyLists);
                }
            }
        }
    }

    void CartoCSSCompiler::buildLayerAttachment(const FilteredPropertyList& propertyList, std::list<AttachmentPropertySets>& layerAttachments) const {
        std::list<PropertySet> propertySets;
        for (const FilteredProperty& prop : propertyList.properties) {
            for (auto propertySetIt = propertySets.begin(); propertySetIt != propertySets.end(); propertySetIt++) {
                // Check if this attribute is already set for given property set
                if (auto existingProp = propertySetIt->findProperty(prop.property.getField())) {
                    if (existingProp->getSpecificity() >= prop.property.getSpecificity()) {
                        continue;
                    }
                    if (std::visit(ExpressionDeepEqualsChecker(), existingProp->getExpression(), prop.property.getExpression())) {
                        continue;
                    }
                }

                // Build new property set by setting the attribute and combining filters
                PropertySet propertySet(*propertySetIt);
                if (!propertySet.mergeFilters(prop.filters)) {
                    continue;
                }
                propertySet.insertProperty(prop.property);

                // Check if the property set is redundant (existing filters already cover it)
                bool redundant = false;
                for (auto it = propertySetIt; it != propertySets.begin(); ) {
                    it--;
                    if (it->covers(propertySet)) {
                        redundant = true;
                        break;
                    }
                }
                if (redundant) {
                    continue;
                }

                // If filters did not change, replace existing filter otherwise we must insert the new filter and keep old one
                if (propertySet.getFilters() == propertySetIt->getFilters()) {
                    *propertySetIt = std::move(propertySet);
                }
                else {
                    propertySets.insert(propertySetIt, std::move(propertySet));
                }
            }

            // Build new property set
            PropertySet propertySet;
            if (!propertySet.mergeFilters(prop.filters)) {
                continue;
            }
            propertySet.insertProperty(prop.property);

            // Check if the property set is redundant (existing filters already cover it)
            bool redundant = false;
            for (auto it = propertySets.end(); it != propertySets.begin(); ) {
                it--;
                if (it->covers(propertySet)) {
                    redundant = true;
                    break;
                }
            }
            if (redundant) {
                continue;
            }

            // Add the built property set to the list
            propertySets.push_back(std::move(propertySet));
        }

        // Add layer attachment
        layerAttachments.push_back(AttachmentPropertySets(propertyList.attachment, std::move(propertySets)));
    }

    std::shared_ptr<Predicate> CartoCSSCompiler::getPredicatePtr(const Predicate& pred, std::list<std::pair<Predicate, std::shared_ptr<Predicate>>>& predList) {
        auto it = std::find_if(predList.begin(), predList.end(), [&pred](const std::pair<Predicate, const std::shared_ptr<Predicate>>& predPair) {
            return predPair.first == pred;
        });
        if (it == predList.end()) {
            it = predList.insert(it, std::make_pair(pred, std::make_shared<Predicate>(pred)));
        }
        return it->second;
    }
    
    Property::RuleSpecificity CartoCSSCompiler::calculateRuleSpecificity(const std::vector<std::shared_ptr<Predicate>>& predicates, int order) {
        struct PredicateCounter {
            void operator() (const MapPredicate&) { }
            void operator() (const LayerPredicate&) { layers++; }
            void operator() (const ClassPredicate&) { classes++; }
            void operator() (const AttachmentPredicate&) { }
            void operator() (const OpPredicate&) { filters++; }

            int layers = 0;
            int classes = 0;
            int filters = 0;
        };

        PredicateCounter counter;
        for (const std::shared_ptr<Predicate>& pred : predicates) {
            std::visit(counter, *pred);
        }
        return std::make_tuple(counter.layers, counter.classes, counter.filters, order);
    }
} }
