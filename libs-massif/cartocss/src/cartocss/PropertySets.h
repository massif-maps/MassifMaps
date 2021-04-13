/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_PROPERTYSETS_H_
#define _CARTO_CARTOCSS_PROPERTYSETS_H_

#include "Value.h"
#include "Expression.h"
#include "ExpressionUtils.h"
#include "Predicate.h"
#include "PredicateUtils.h"

#include <memory>
#include <string>
#include <vector>
#include <list>
#include <utility>
#include <limits>
#include <functional>
#include <algorithm>

namespace carto { namespace css {
    class Property final {
    public:
        using RuleSpecificity = std::tuple<int, int, int, int>; // layers, filters, classes, order

        Property() = default;
        explicit Property(std::string field, Expression expr, RuleSpecificity specificity) : _field(std::move(field)), _expression(std::move(expr)), _specificity(specificity) { }

        const std::string& getField() const { return _field; }
        const Expression& getExpression() const { return _expression; }
        const RuleSpecificity& getSpecificity() const { return _specificity; }
        int getRuleOrder() const { return std::get<3>(_specificity); }

        bool operator == (const Property& other) const {
            return _field == other._field && std::visit(ExpressionDeepEqualsChecker(), _expression, other._expression) && _specificity == other._specificity;
        }

        bool operator != (const Property& other) const {
            return !(*this == other);
        }

    private:
        std::string _field;
        Expression _expression;
        RuleSpecificity _specificity;
    };

    class PropertySet final {
    public:
        PropertySet() = default;
        explicit PropertySet(std::vector<std::shared_ptr<const Predicate>> filters, std::vector<std::shared_ptr<const Property>> properties) : _filters(std::move(filters)), _properties(std::move(properties)) { }

        const std::vector<std::shared_ptr<const Predicate>>& getFilters() const { return _filters; }
        const std::vector<std::shared_ptr<const Property>>& getProperties() const { return _properties; }

        std::shared_ptr<const Property> findProperty(const std::string& field) const {
            auto it = std::find_if(_properties.begin(), _properties.end(), [&field](const std::shared_ptr<const Property>& prop) { return prop->getField() == field; });
            return it != _properties.end() ? *it : std::shared_ptr<const Property>();
        }

        void insertProperty(const std::shared_ptr<const Property>& prop) {
            auto it = std::lower_bound(_properties.begin(), _properties.end(), prop, [](const std::shared_ptr<const Property>& prop1, const std::shared_ptr<const Property>& prop2) { return prop1->getField() < prop2->getField(); });
            if (it != _properties.end() && (*it)->getField() == prop->getField()) {
                *it = prop;
            } else {
                _properties.insert(it, prop);
            }
        }

        bool mergeFilters(const std::vector<std::shared_ptr<const Predicate>>& filters) {
            for (const std::shared_ptr<const Predicate>& filter : filters) {
                for (const std::shared_ptr<const Predicate>& existingFilter : _filters) {
                    boost::tribool intersects = std::visit(PredicateIntersectsChecker(), *filter, *existingFilter);
                    if (!intersects) {
                        return false;
                    }
                }

                bool insert = true;
                for (std::shared_ptr<const Predicate>& existingFilter : _filters) {
                    boost::tribool contains = std::visit(PredicateContainsChecker(), *filter, *existingFilter);
                    if (contains) {
                        insert = false;
                        break;
                    }
                    boost::tribool containsExisting = std::visit(PredicateContainsChecker(), *existingFilter, *filter);
                    if (containsExisting) {
                        existingFilter = filter;
                        insert = false;
                        break;
                    }
                }
                if (insert) {
                    _filters.push_back(filter);
                }
            }
            return true;
        }

        bool covers(const PropertySet& propertySet) const {
            return std::all_of(_filters.begin(), _filters.end(), [&](const std::shared_ptr<const Predicate>& filter) {
                if (std::find(propertySet.getFilters().begin(), propertySet.getFilters().end(), filter) != propertySet.getFilters().end()) {
                    return true;
                }
                return std::any_of(propertySet.getFilters().begin(), propertySet.getFilters().end(), [&filter](const std::shared_ptr<const Predicate>& propFilter) {
                    boost::tribool contains = std::visit(PredicateContainsChecker(), *filter, *propFilter);
                    return contains;
                });
            });
        }

        bool operator == (const PropertySet& other) const {
            if (_filters == other._filters && _properties.size() == other._properties.size()) {
                return std::equal(_properties.begin(), _properties.end(), other._properties.begin(), [](const std::shared_ptr<const Property>& prop1, const std::shared_ptr<const Property>& prop2) {
                    return prop1 == prop2 || *prop1 == *prop2;
                });
            }
            return false;
        }

        bool operator != (const PropertySet& other) const {
            return !(*this == other);
        }

    private:
        std::vector<std::shared_ptr<const Predicate>> _filters;
        std::vector<std::shared_ptr<const Property>> _properties;
    };

    class AttachmentPropertySets final {
    public:
        AttachmentPropertySets() = default;
        explicit AttachmentPropertySets(std::string attachment, std::list<PropertySet> propertySets) : _attachment(std::move(attachment)), _propertySets(std::move(propertySets)) { }

        const std::string& getAttachment() const { return _attachment; }
        const std::list<PropertySet>& getPropertySets() const { return _propertySets; }

        int calculateOrder() const {
            int order = std::numeric_limits<int>::max();
            for (const PropertySet& propertySet : _propertySets) {
                for (const std::shared_ptr<const Property>& prop : propertySet.getProperties()) {
                    order = std::min(order, prop->getRuleOrder());
                }
            }
            return order;
        }

        bool operator == (const AttachmentPropertySets& other) const {
            return _attachment == other._attachment && _propertySets == other._propertySets;
        }

        bool operator != (const AttachmentPropertySets& other) const {
            return !(*this == other);
        }

    private:
        std::string _attachment;
        std::list<PropertySet> _propertySets;
    };
} }

#endif
