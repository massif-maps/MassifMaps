/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_CARTOCSSMAPNIKTRANSLATOR_H_
#define _CARTO_CARTOCSS_CARTOCSSMAPNIKTRANSLATOR_H_

#include "Expression.h"
#include "Predicate.h"
#include "PropertySets.h"
#include "CartoCSSCompiler.h"
#include "mapnikvt/Map.h"
#include "mapnikvt/Rule.h"
#include "mapnikvt/Expression.h"
#include "mapnikvt/Predicate.h"
#include "mapnikvt/Logger.h"

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <unordered_set>
#include <unordered_map>

namespace carto { namespace css {
    class CartoCSSMapnikTranslator {
    public:
        class TranslatorException : public std::runtime_error {
        public:
            explicit TranslatorException(const std::string& msg) : runtime_error(msg) { }
        };

        explicit CartoCSSMapnikTranslator(std::shared_ptr<mvt::Logger> logger) : _logger(std::move(logger)) { }
        virtual ~CartoCSSMapnikTranslator() = default;

        virtual std::shared_ptr<mvt::Rule> buildRule(const PropertySet& propertySet, const std::shared_ptr<mvt::Map>& map, int minZoom, int maxZoom) const;

        virtual std::shared_ptr<mvt::Symbolizer> buildSymbolizer(const std::string& symbolizerType, const std::list<Property>& properties, const std::shared_ptr<mvt::Map>& map) const;

        static std::string buildValueString(const Value& value, bool stringExpr);

        static std::string buildExpressionString(const Expression& expr, bool stringExpr);

        static std::string buildFunctionExpressionString(const FunctionExpression& funcExpr, bool topLevel);

        static std::optional<mvt::Predicate> buildPredicate(const Predicate& pred);

        static mvt::ComparisonPredicate::Op buildComparisonOp(OpPredicate::Op op);

        static mvt::Value buildValue(const Value& val);

    protected:
        virtual bool isStringExpression(const std::string& propertyName) const;

        virtual std::string getPropertySymbolizerId(const std::string& propertyName) const;

        virtual void setSymbolizerParameter(const std::shared_ptr<mvt::Symbolizer>& symbolizer, const std::string& name, const Expression& expr, bool stringExpr) const;

        const std::shared_ptr<mvt::Logger> _logger;

    private:
        static const std::vector<std::pair<UnaryExpression::Op, std::string>> _unaryOpTable;
        static const std::vector<std::pair<BinaryExpression::Op, std::string>> _binaryOpTable;
        static const std::vector<std::pair<OpPredicate::Op, mvt::ComparisonPredicate::Op>> _predicateOpTable;

        static const std::unordered_map<std::string, int> _stringFuncs;
        static const std::unordered_map<std::string, int> _mathFuncs;
        static const std::unordered_set<std::string> _interpolationFuncs;

        static const std::vector<std::string> _symbolizerList;
        static const std::unordered_set<std::string> _symbolizerNonStringProperties;
        static const std::unordered_map<std::string, std::string> _symbolizerPropertyMap;
    };
} }

#endif
