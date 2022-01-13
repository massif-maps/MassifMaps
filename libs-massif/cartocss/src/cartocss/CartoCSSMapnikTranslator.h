/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_CARTOCSSMAPNIKTRANSLATOR_H_
#define _CARTO_CARTOCSS_CARTOCSSMAPNIKTRANSLATOR_H_

#include "Expression.h"
#include "ExpressionUtils.h"
#include "Predicate.h"
#include "PredicateUtils.h"
#include "PropertySets.h"
#include "CartoCSSCompiler.h"
#include "mapnikvt/Map.h"
#include "mapnikvt/Rule.h"
#include "mapnikvt/Expression.h"
#include "mapnikvt/Predicate.h"
#include "mapnikvt/Logger.h"

#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <vector>
#include <optional>
#include <unordered_map>

namespace carto::css {
    class CartoCSSMapnikTranslator {
    public:
        class TranslatorException : public std::runtime_error {
        public:
            explicit TranslatorException(const std::string& msg) : runtime_error(msg) { }
        };

        explicit CartoCSSMapnikTranslator(std::shared_ptr<mvt::Logger> logger) : _logger(std::move(logger)) { }
        virtual ~CartoCSSMapnikTranslator() = default;

        std::shared_ptr<const mvt::Rule> buildRule(const PropertySet& propertySet, const std::shared_ptr<mvt::Map>& map, int minZoom, int maxZoom) const;

        std::shared_ptr<const mvt::Filter> buildFilter(const std::vector<std::shared_ptr<const Predicate>>& filters) const;

        std::shared_ptr<const mvt::Symbolizer> buildSymbolizer(const std::string& symbolizerType, const std::vector<std::shared_ptr<const Property>>& properties, const std::shared_ptr<mvt::Map>& map) const;

        static mvt::Expression buildExpression(const Expression& expr);

        static mvt::Expression buildFunctionExpression(const FunctionExpression& funcExpr);

        static std::optional<mvt::Predicate> buildPredicate(const Predicate& pred);

        static mvt::ComparisonPredicate::Op buildComparisonOp(OpPredicate::Op op);

        static mvt::Value buildValue(const Value& val);

    protected:
        virtual std::string getPropertySymbolizerId(const std::string& propertyName) const;

        virtual std::shared_ptr<const mvt::Symbolizer> createSymbolizer(const std::string& symbolizerType, const std::vector<std::shared_ptr<const Property>>& properties, const std::shared_ptr<mvt::Map>& map) const;

        const std::shared_ptr<mvt::Logger> _logger;

    private:
        static const std::vector<std::pair<UnaryExpression::Op, mvt::UnaryExpression::Op>> _unaryOpTable;
        static const std::vector<std::pair<BinaryExpression::Op, mvt::BinaryExpression::Op>> _binaryOpTable;
        static const std::vector<std::pair<BinaryExpression::Op, mvt::ComparisonPredicate::Op>> _comparisonOpTable;
        static const std::vector<std::pair<OpPredicate::Op, mvt::ComparisonPredicate::Op>> _predicateOpTable;

        static const std::unordered_map<std::string, std::variant<mvt::UnaryExpression::Op, mvt::BinaryExpression::Op, mvt::TertiaryExpression::Op>> _basicFuncMap;
        static const std::unordered_map<std::string, mvt::InterpolateExpression::Method> _interpolationFuncMap;
        static const std::unordered_map<std::string, std::type_index> _transformFuncMap;

        static const std::vector<std::string> _symbolizerList;
        static const std::unordered_map<std::string, std::string> _symbolizerPropertyMap;

        struct FilterKey {
            std::vector<std::shared_ptr<const Predicate>> filters;

            bool operator == (const FilterKey& other) const {
                if (filters.size() != other.filters.size()) {
                    return false;
                }
                for (std::size_t i = 0; i < filters.size(); i++) {
                    const Predicate& pred = *filters[i];
                    const Predicate& otherPred = *other.filters[i];
                    if (pred != otherPred) {
                        return false;
                    }
                }
                return true;
            }

            bool operator != (const FilterKey& other) const {
                return !(*this == other);
            }
        };

        struct FilterKeyHasher {
            std::size_t operator() (const FilterKey& key) const {
                std::size_t hash = key.filters.size();
                for (const std::shared_ptr<const Predicate>& pred : key.filters) {
                    hash = hash * 17 ^ pred->index();
                }
                return hash;
            }
        };

        struct SymbolizerKey {
            std::string symbolizerType;
            std::shared_ptr<const mvt::Map> map;
            std::vector<std::shared_ptr<const Property>> properties;

            bool operator == (const SymbolizerKey& other) const {
                if (symbolizerType != other.symbolizerType || map != other.map || properties.size() != other.properties.size()) {
                    return false;
                }
                for (std::size_t i = 0; i < properties.size(); i++) {
                    const Property& prop = *properties[i];
                    const Property& otherProp = *other.properties[i];
                    if (prop.getField() != otherProp.getField()) {
                        return false;
                    }
                    if (!std::visit(ExpressionDeepEqualsChecker(), prop.getExpression(), otherProp.getExpression())) {
                        return false;
                    }
                }
                return true;
            }

            bool operator != (const SymbolizerKey& other) const {
                return !(*this == other);
            }
        };

        struct SymbolizerKeyHasher {
            std::size_t operator() (const SymbolizerKey& key) const {
                std::size_t hash = std::hash<std::string>()(key.symbolizerType) ^ std::hash<std::shared_ptr<const mvt::Map>>()(key.map);
                for (const std::shared_ptr<const Property>& prop : key.properties) {
                    hash = hash * 17 ^ std::hash<std::string>()(prop->getField()) ^ prop->getExpression().index();
                }
                return hash;
            }
        };

        mutable std::unordered_map<FilterKey, std::shared_ptr<const mvt::Filter>, FilterKeyHasher> _filterCache;
        mutable std::unordered_map<SymbolizerKey, std::shared_ptr<const mvt::Symbolizer>, SymbolizerKeyHasher> _symbolizerCache;
        mutable std::mutex _cacheMutex;
    };
}

#endif
