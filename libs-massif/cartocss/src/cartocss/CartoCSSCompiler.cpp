#include "CartoCSSCompiler.h"
#include "ExpressionUtils.h"
#include "PredicateUtils.h"

#include <set>

namespace massif::css {
    namespace {
        // Three-state predicate result, in a form that can be compared as a block (boost::tribool
        // compares to a tribool, so two indeterminate results do not test equal).
        enum : unsigned char { PREDICATE_FALSE = 0, PREDICATE_TRUE = 1, PREDICATE_INDETERMINATE = 2 };
    }

    void CartoCSSCompiler::compileMap(const StyleSheet& styleSheet, std::map<std::string, Expression>& mapProperties, std::map<std::string, Value>& constantFieldMap) const {
        // Build flat property lists
        std::map<std::string, Expression> variableMap;
        PredicateContext context;
        context.expressionContext = _context;
        context.expressionContext.variableMap = &variableMap;
        context.expressionContext.constantFieldMap = &constantFieldMap;

        FilteredPropertyState state;
        std::list<FilteredPropertyList> propertyLists;
        buildPropertyLists(styleSheet, context, state, propertyLists);

        // Gather map properties from property lists
        for (const FilteredPropertyList& propertyList : propertyLists) {
            if (propertyList.attachment.empty()) {
                PredicateEvaluator predEvaluator(context);

                for (const FilteredProperty& property : propertyList.properties) {
                    bool unreachableProp = false;
                    for (std::size_t filter : property.filters) {
                        boost::tribool result = std::visit(predEvaluator, *state.getPredicate(filter));
                        if (!result || boost::indeterminate(result)) {
                            unreachableProp = true;
                            break;
                        }
                    }
                    if (unreachableProp) {
                        continue;
                    }

                    std::shared_ptr<const Property> prop = state.getProperty(property.property);
                    mapProperties[prop->getField()] = prop->getExpression();
                }
            }
        }
    }
    
    void CartoCSSCompiler::compileLayer(const StyleSheet& styleSheet, const std::string& layerName, int minZoom, int maxZoom, std::map<std::pair<int, int>, std::list<AttachmentPropertySets>>& layerZoomAttachments, std::map<std::string, Value>& constantFieldMap) const {
        // Build flat property lists
        std::map<std::string, Expression> variableMap;
        PredicateContext context;
        context.layerName = layerName;
        context.expressionContext = _context;
        context.expressionContext.variableMap = &variableMap;
        context.expressionContext.constantFieldMap = &constantFieldMap;

        FilteredPropertyState state;
        std::list<FilteredPropertyList> propertyLists;
        buildPropertyLists(styleSheet, context, state, propertyLists);

        // Sort the properties by decreasing specificity. buildLayerAttachment relies on this order:
        // the property that already set a field always wins over the ones that follow.
        for (FilteredPropertyList& propertyList : propertyLists) {
            std::stable_sort(propertyList.properties.begin(), propertyList.properties.end(), [&state](const FilteredProperty& property1, const FilteredProperty& property2) {
                return state.getPropertyRef(property1.property).getSpecificity() > state.getPropertyRef(property2.property).getSpecificity();
            });
        }

        // Build layer attachments for each zoom level
        int prevZoom = minZoom, zoom = minZoom;
        std::list<FilteredPropertyList> prevOptimizedPropertyLists;
        std::list<AttachmentPropertySets> prevLayerAttachments;
        std::vector<unsigned char> predicateResults, prevPredicateResults;
        for (; zoom < maxZoom; zoom++) {
            std::map<std::string, Value> predefinedFieldMap;
            context.expressionContext.predefinedFieldMap = &predefinedFieldMap;
            context.expressionContext.constantFieldMap = &constantFieldMap;
            (*context.expressionContext.predefinedFieldMap)["zoom"] = Value(static_cast<long long>(zoom));
            PredicateEvaluator predEvaluator(context);

            // A predicate evaluates the same for every property that references it at this zoom,
            // and a layer's properties reference the same few dozen predicates thousands of times.
            predicateResults.clear();
            predicateResults.reserve(state.getPredicateCount());
            for (std::size_t predicate = 0; predicate < state.getPredicateCount(); predicate++) {
                boost::tribool result = std::visit(predEvaluator, *state.getPredicate(predicate));
                predicateResults.push_back(boost::indeterminate(result) ? PREDICATE_INDETERMINATE : (result ? PREDICATE_TRUE : PREDICATE_FALSE));
            }

            // The optimized property lists are a pure function of these results, so identical
            // results at the next zoom mean identical lists and identical attachments. Most zooms
            // of a layer land on a range that was already built - comparing 80 bytes here replaces
            // rebuilding and deep-comparing every property of the layer.
            if (zoom > minZoom && predicateResults == prevPredicateResults) {
                continue;
            }
            prevPredicateResults = predicateResults;

            // Evaluate and optimize property lists
            std::list<FilteredPropertyList> optimizedPropertyLists;
            for (const FilteredPropertyList& propertyList : propertyLists) {
                FilteredPropertyList& optimizedPropertyList = optimizedPropertyLists.emplace_back();
                optimizedPropertyList.attachment = propertyList.attachment;
                optimizedPropertyList.properties.reserve(propertyList.properties.size());
                for (const FilteredProperty& property : propertyList.properties) {
                    // Check if this property is reachable and remove always true conditions
                    std::vector<std::size_t> optimizedFilters;
                    optimizedFilters.reserve(property.filters.size());
                    bool unreachableProp = false;
                    for (std::size_t filter : property.filters) {
                        unsigned char result = predicateResults[filter];
                        if (result == PREDICATE_FALSE) {
                            unreachableProp = true;
                            break;
                        }
                        if (result == PREDICATE_INDETERMINATE) { // keep only indeterminate filters, ignore always true filters
                            optimizedFilters.push_back(filter);
                        }
                    }
                    if (unreachableProp) {
                        continue;
                    }

                    // Store property with optimized filter list
                    FilteredProperty& optimizedProperty = optimizedPropertyList.properties.emplace_back();
                    optimizedProperty.property = property.property;
                    optimizedProperty.filters = std::move(optimizedFilters);
                }
            }

            // Check if the property lists changed compared to the previous zoom level
            if (optimizedPropertyLists != prevOptimizedPropertyLists) {
                // Build attachments
                std::list<AttachmentPropertySets> layerAttachments;
                for (const FilteredPropertyList& optimizedPropertyList : optimizedPropertyLists) {
                    buildLayerAttachment(optimizedPropertyList, state, layerAttachments);
                }

                // Check if attachments changed compared to the previous attachments
                if (layerAttachments != prevLayerAttachments) {
                    if (zoom > prevZoom) {
                        layerZoomAttachments[std::make_pair(prevZoom, zoom)] = std::move(prevLayerAttachments);
                    }
                    prevLayerAttachments = std::move(layerAttachments);
                    prevZoom = zoom;
                }

                prevOptimizedPropertyLists = std::move(optimizedPropertyLists);
            }
        }
        if (zoom > prevZoom) {
            layerZoomAttachments[std::make_pair(prevZoom, zoom)] = std::move(prevLayerAttachments);
        }
    }

    void CartoCSSCompiler::buildPropertyLists(const StyleSheet& styleSheet, PredicateContext& context, FilteredPropertyState& state, std::list<FilteredPropertyList>& propertyLists) const {
        for (const StyleSheet::Element& element : styleSheet.getElements()) {
            if (auto decl = std::get_if<VariableDeclaration>(&element)) {
                if (context.expressionContext.variableMap->find(decl->getVariable()) == context.expressionContext.variableMap->end()) {
                    (*context.expressionContext.variableMap)[decl->getVariable()] = decl->getExpression();
                }
            }
            else if (auto ruleSet = std::get_if<RuleSet>(&element)) {
                buildPropertyList(*ruleSet, context, std::string(), std::vector<std::size_t>(), state, propertyLists);
            }
        }

        ExpressionEvaluator exprEvaluator(context.expressionContext);
        // The same property appears in as many lists as it has attachments, and evaluating it does
        // not depend on where it appears - so evaluate each one once.
        std::unordered_map<std::size_t, std::size_t> evaluatedProperties;
        for (FilteredPropertyList& propertyList : propertyLists) {
            for (FilteredProperty& property : propertyList.properties) {
                auto evaluatedIt = evaluatedProperties.find(property.property);
                if (evaluatedIt != evaluatedProperties.end()) {
                    property.property = evaluatedIt->second;
                    continue;
                }

                std::size_t sourceProperty = property.property;
                const Property& prop = state.getPropertyRef(sourceProperty);
                Expression evaluatedExpr = std::visit(exprEvaluator, prop.getExpression());
                if (evaluatedExpr != prop.getExpression()) {
                    property.property = state.insertProperty(Property(prop.getField(), std::move(evaluatedExpr), prop.getSpecificity()));
                }
                evaluatedProperties[sourceProperty] = property.property;
            }
        }
    }
    
    void CartoCSSCompiler::buildPropertyList(const RuleSet& ruleSet, const PredicateContext& context, const std::string& existingAttachment, const std::vector<std::size_t>& existingFilters, FilteredPropertyState& state, std::list<FilteredPropertyList>& propertyLists) const {
        // List of selectors to use
        const std::vector<Selector>* selectors = &ruleSet.getSelectors();
        if (selectors->empty() && !context.layerName.empty()) {
            static const std::vector<Selector> emptySelectorSet { Selector() };
            selectors = &emptySelectorSet;
        }
        PredicateEvaluator predEvaluator(context);

        // Process all selectors
        for (const Selector& selector : *selectors) {
            // Build filters for given selector. Check if the filter list is 'reachable'.
            std::vector<std::size_t> filters = existingFilters;
            std::string attachment = existingAttachment;
            bool unreachableProp = false;
            for (const Predicate& pred : selector.getPredicates()) {
                if (auto attachmentPred = std::get_if<AttachmentPredicate>(&pred)) {
                    attachment += "::" + attachmentPred->getAttachment();
                    continue;
                }
                if (auto layerPred = std::get_if<LayerPredicate>(&pred)) {
                    if (_ignoreLayerPredicates) {
                        continue;
                    }
                }

                boost::tribool result = std::visit(predEvaluator, pred);
                if (!result) {
                    unreachableProp = true;
                    break;
                } else if (std::get_if<ConstOpPredicate>(&pred)) {
                    continue;
                }
                filters.push_back(state.insertPredicate(pred));
            }
            if (unreachableProp) {
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
                    auto propertyListsIt = std::find_if(propertyLists.begin(), propertyLists.end(), [&attachment](const FilteredPropertyList& propertyList) {
                        return propertyList.attachment == attachment;
                    });
                    if (propertyListsIt == propertyLists.end()) {
                        FilteredPropertyList propertyList;
                        propertyList.attachment = attachment;
                        propertyListsIt = propertyLists.insert(propertyListsIt, propertyList);
                    }
                    
                    // Add property
                    Property::RuleSpecificity specificity = calculateRuleSpecificity(filters, state, decl->getOrder());
                    FilteredProperty& property = propertyListsIt->properties.emplace_back();
                    property.property = state.insertProperty(Property(decl->getField(), decl->getExpression(), specificity));
                    property.filters = filters;
                }
                else if (auto subRuleSet = std::get_if<RuleSet>(&element)) {
                    // Recurse with subrule
                    buildPropertyList(*subRuleSet, context, attachment, filters, state, propertyLists);
                }
            }
        }
    }

    void CartoCSSCompiler::buildLayerAttachment(const FilteredPropertyList& propertyList, const FilteredPropertyState& state, std::list<AttachmentPropertySets>& layerAttachments) const {
        // Build preliminary property sets, with optimized internal structures
        std::list<FilteredPropertySet> propertySets;
        FilteredPropertySet trialPropertySet; // reused: assigning into it keeps the two vector
                                              // buffers, one per property per property set otherwise
        for (const FilteredProperty& property : propertyList.properties) {
            std::size_t fieldId = state.getPropertyFieldId(property.property);

            for (auto propertySetIt = propertySets.begin(); propertySetIt != propertySets.end(); propertySetIt++) {
                // Check if this attribute is already set for given property set. The property that
                // set it came earlier in this list, which compileLayer sorted by decreasing
                // specificity, so it always wins over this one - no need to compare them.
                if (state.propertySetHasField(*propertySetIt, fieldId)) {
                    continue;
                }

                // Build new property set by setting the attribute and combining filters
                if (!state.canMergePropertySetProperty(*propertySetIt, property)) {
                    continue;
                }
                trialPropertySet = *propertySetIt;
                if (!state.mergePropertySetProperty(trialPropertySet, property)) {
                    continue;
                }

                // Check if the property set is redundant (existing filters already cover it)
                if (std::any_of(propertySets.begin(), propertySetIt, [&state, &trialPropertySet](const FilteredPropertySet& existingPropertySet) {
                    return state.testPropertySetFilterCover(existingPropertySet, trialPropertySet);
                })) {
                    continue;
                }

                // If filters did not change, replace existing filter otherwise we must insert the new filter and keep old one
                if (trialPropertySet.filters == propertySetIt->filters) {
                    *propertySetIt = std::move(trialPropertySet);
                }
                else {
                    propertySets.insert(propertySetIt, std::move(trialPropertySet));
                }
            }

            // Build new property set
            FilteredPropertySet propertySet;
            if (!state.mergePropertySetProperty(propertySet, property)) {
                continue;
            }

            // Check if the property set is redundant (existing filters already cover it)
            if (std::any_of(propertySets.begin(), propertySets.end(), [&state, &propertySet](const FilteredPropertySet& existingPropertySet) {
                return state.testPropertySetFilterCover(existingPropertySet, propertySet);
            })) {
                continue;
            }

            // Add the built property set to the list
            propertySets.push_back(std::move(propertySet));
        }

        // Build final compiled property sets
        std::list<PropertySet> compiledPropertySets;
        for (const FilteredPropertySet& propertySet : propertySets) {
            std::vector<std::shared_ptr<const Predicate>> compiledFilters;
            compiledFilters.reserve(propertySet.filters.size());
            for (std::size_t filter : propertySet.filters) {
                compiledFilters.push_back(state.getPredicate(filter));
            }
            
            std::vector<std::shared_ptr<const Property>> compiledProperties;
            compiledProperties.reserve(propertySet.properties.size());
            for (std::size_t property : propertySet.properties) {
                compiledProperties.push_back(state.getProperty(property));
            }
            
            compiledPropertySets.push_back(PropertySet(std::move(compiledFilters), std::move(compiledProperties)));
        }

        // Add layer attachment
        layerAttachments.push_back(AttachmentPropertySets(propertyList.attachment, std::move(compiledPropertySets)));
    }

    Property::RuleSpecificity CartoCSSCompiler::calculateRuleSpecificity(const std::vector<std::size_t>& filters, const FilteredPropertyState& state, int order) {
        struct PredicateCounter {
            void operator() (const MapPredicate&) { }
            void operator() (const LayerPredicate&) { layers++; }
            void operator() (const ClassPredicate&) { classes++; }
            void operator() (const AttachmentPredicate&) { }
            void operator() (const ConstOpPredicate&) { }
            void operator() (const OpPredicate&) { filters++; }
            void operator() (const OpConstPredicate&) { filters++; }
            void operator() (const OpParamPredicate&) { filters++; }
            void operator() (const WhenPredicate&) { filters++; }

            int layers = 0;
            int classes = 0;
            int filters = 0;
        };

        PredicateCounter counter;
        for (std::size_t filter : filters) {
            std::visit(counter, *state.getPredicate(filter));
        }
        return std::make_tuple(counter.layers, counter.classes, counter.filters, order);
    }
}
