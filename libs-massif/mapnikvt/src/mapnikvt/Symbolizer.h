/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_SYMBOLIZER_H_
#define _CARTO_MAPNIKVT_SYMBOLIZER_H_

#include "FeatureCollection.h"
#include "Expression.h"
#include "ExpressionContext.h"
#include "ParserUtils.h"
#include "SymbolizerContext.h"
#include "SymbolizerParameter.h"
#include "Logger.h"
#include "vt/Color.h"
#include "vt/Transform.h"
#include "vt/TileLayerBuilder.h"

#include <memory>
#include <set>

namespace carto { namespace mvt {
    class Symbolizer {
    public:
        virtual ~Symbolizer() = default;

        std::set<std::string> getParameterNames() const;
        SymbolizerParameter* getParameter(const std::string& paramName);
        const SymbolizerParameter* getParameter(const std::string& paramName) const;

        virtual void build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const = 0;

    protected:
        explicit Symbolizer(std::shared_ptr<Logger> logger) : _logger(std::move(logger)) {
            bindParameter("comp-op", &_compOp);
        }

        void bindParameter(const std::string& paramName, SymbolizerParameter* param);
        void unbindParameter(const std::string& paramName);

        static long long convertId(const Value& val);
        static long long generateId();
        static long long combineId(long long globalId, std::size_t hash);

        CompOpParameter _compOp = CompOpParameter("src-over");

        std::shared_ptr<Logger> _logger;

    private:
        std::map<std::string, SymbolizerParameter*> _parameterMap;
    };
} }

#endif
