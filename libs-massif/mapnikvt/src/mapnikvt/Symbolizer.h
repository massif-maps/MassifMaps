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

namespace carto::mvt {
    class Symbolizer {
    public:
        using FeatureProcessor = std::function<void(const FeatureCollection& featureCollection, vt::TileLayerBuilder& layerBuilder)>;

        virtual ~Symbolizer() = default;

        std::set<std::string> getParameterNames() const;
        SymbolizerParameter* getParameter(const std::string& paramName);
        const SymbolizerParameter* getParameter(const std::string& paramName) const;

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const = 0;

    protected:
        explicit Symbolizer(std::shared_ptr<Logger> logger) : _logger(std::move(logger)) {
            bindParameter("comp-op", &_compOp);
        }

        void bindParameter(const std::string& paramName, SymbolizerParameter* param);
        void unbindParameter(const std::string& paramName);

        static long long convertId(const Value& val);
        static long long generateId();
        static long long combineId(long long id, std::size_t hash);

        const std::shared_ptr<Logger> _logger;

        CompOpParameter _compOp = CompOpParameter("src-over");

    private:
        std::map<std::string, SymbolizerParameter*> _parameterMap;
    };
}

#endif
