/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_LINESYMBOLIZER_H_
#define _CARTO_MAPNIKVT_LINESYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace carto::mvt {
    class LineSymbolizer : public GeometrySymbolizer {
    public:
        explicit LineSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            bindProperty("stroke", &_stroke);
            bindProperty("stroke-width", &_strokeWidth);
            bindProperty("stroke-opacity", &_strokeOpacity);
            bindProperty("stroke-linejoin", &_strokeLinejoin);
            bindProperty("stroke-linecap", &_strokeLinecap);
            bindProperty("stroke-dasharray", &_strokeDashArray);
            bindProperty("stroke-miterlimit", &_strokeMiterLimit);
            bindProperty("offset", &_offset);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static constexpr int DASH_SUPERSAMPLING_FACTOR = 2;
        static constexpr float DASH_PATTERN_SCALE = 0.75f;
        static constexpr float DASH_MITER_DOT_LIMIT = 0.2f;
        static constexpr float SPLIT_DOT_LIMIT = -0.95f;

        static std::shared_ptr<vt::BitmapPattern> createDashBitmapPattern(const std::vector<float>& strokeDashArray, float height, vt::LineCapMode lineCap);

        ColorFunctionProperty _stroke = ColorFunctionProperty("#000000");
        FloatFunctionProperty _strokeWidth = FloatFunctionProperty(1.0f);
        FloatFunctionProperty _strokeOpacity = FloatFunctionProperty(1.0f);
        LineJoinModeProperty _strokeLinejoin = LineJoinModeProperty("miter");
        LineCapModeProperty _strokeLinecap = LineCapModeProperty("butt");
        StringProperty _strokeDashArray = StringProperty("");
        FloatFunctionProperty _strokeMiterLimit = FloatFunctionProperty(4.0f);
        FloatFunctionProperty _offset = FloatFunctionProperty(0.0f);

        ColorFunctionBuilder _strokeFuncBuilder;
    };
}

#endif
