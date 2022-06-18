/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_POLYGONPATTERNSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_POLYGONPATTERNSYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace carto::mvt {
    class PolygonPatternSymbolizer : public GeometrySymbolizer {
    public:
        explicit PolygonPatternSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            bindProperty("file", &_file);
            bindProperty("fill", &_fill);
            bindProperty("opacity", &_opacity);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static constexpr float PATTERN_SCALE = 0.75f;

        StringProperty _file;
        ColorFunctionProperty _fill = ColorFunctionProperty("#ffffff");
        FloatFunctionProperty _opacity = FloatFunctionProperty(1.0f);

        ColorFunctionBuilder _fillFuncBuilder;
    };
}

#endif
