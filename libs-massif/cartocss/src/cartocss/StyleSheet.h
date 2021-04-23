/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_STYLESHEET_H_
#define _CARTO_CARTOCSS_STYLESHEET_H_

#include "Value.h"
#include "Expression.h"
#include "Predicate.h"

#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <map>
#include <utility>
#include <functional>

namespace carto { namespace css {
    class Selector final {
    public:
        Selector() = default;
        explicit Selector(std::vector<Predicate> predicates) : _predicates(std::move(predicates)) { }

        const std::vector<Predicate>& getPredicates() const { return _predicates; }

    private:
        std::vector<Predicate> _predicates; // all must match
    };

    class PropertyDeclaration final {
    public:
        PropertyDeclaration() = default;
        explicit PropertyDeclaration(std::string field, Expression expr, int order) : _field(std::move(field)), _expr(std::move(expr)), _order(order) { }

        const std::string& getField() const { return _field; }
        const Expression& getExpression() const { return _expr; }
        int getOrder() const { return _order; }

    private:
        std::string _field;
        Expression _expr;
        int _order = 0;
    };

    class RuleSet;

    class Block final {
    public:
        using Element = std::variant<PropertyDeclaration, RuleSet>;

        Block() = default;
        explicit Block(std::vector<Element> elements) : _elements(std::move(elements)) { }

        const std::vector<Element>& getElements() const { return _elements; }

    private:
        std::vector<Element> _elements;
    };

    class RuleSet final {
    public:
        RuleSet() = default;
        explicit RuleSet(std::vector<Selector> selectors, Block block) : _selectors(std::move(selectors)), _block(std::move(block)) { }

        const std::vector<Selector>& getSelectors() const { return _selectors; }
        const Block& getBlock() const { return _block; }

    private:
        std::vector<Selector> _selectors; // any may match, or empty
        Block _block;
    };

    class VariableDeclaration final {
    public:
        VariableDeclaration() = default;
        explicit VariableDeclaration(std::string var, Expression expr) : _var(std::move(var)), _expr(std::move(expr)) { }

        const std::string& getVariable() const { return _var; }
        const Expression& getExpression() const { return _expr; }

    private:
        std::string _var;
        Expression _expr;
    };

    class StyleSheet final {
    public:
        using Element = std::variant<VariableDeclaration, RuleSet>;

        StyleSheet() = default;
        explicit StyleSheet(std::vector<Element> elements) : _elements(std::move(elements)) { }

        const std::vector<Element>& getElements() const { return _elements; }

    private:
        std::vector<Element> _elements;
    };
} }

#endif
