/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_LINESYMBOLIZER_H_
#define _CARTO_MAPNIKVT_LINESYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace carto { namespace mvt {
    class LineSymbolizer : public GeometrySymbolizer {
    public:
        explicit LineSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            bindParameter("stroke", &_stroke);
            bindParameter("stroke-width", &_strokeWidth);
            bindParameter("stroke-opacity", &_strokeOpacity);
            bindParameter("stroke-linejoin", &_strokeLinejoin);
            bindParameter("stroke-linecap", &_strokeLinecap);
            bindParameter("stroke-dasharray", &_strokeDashArray);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        inline static constexpr int DASH_SUPERSAMPLING_FACTOR = 2;
        inline static constexpr float DASH_PATTERN_SCALE = 0.75f;

        static std::shared_ptr<vt::BitmapPattern> createDashBitmapPattern(const std::vector<float>& strokeDashArray, int height, vt::LineCapMode lineCap);

        ColorFunctionParameter _stroke = ColorFunctionParameter("#000000");
        FloatFunctionParameter _strokeWidth = FloatFunctionParameter(1.0f);
        FloatFunctionParameter _strokeOpacity = FloatFunctionParameter(1.0f);
        LineJoinModeParameter _strokeLinejoin = LineJoinModeParameter("miter");
        LineCapModeParameter _strokeLinecap = LineCapModeParameter("butt");
        StringParameter _strokeDashArray = StringParameter("");

        ColorFunctionBuilder _strokeFuncBuilder;
    };
} }

#endif
