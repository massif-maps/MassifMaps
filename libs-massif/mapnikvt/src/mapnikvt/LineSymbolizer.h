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
            bindProperty("stroke-width", &_strokeWidth, true); // sizes the stroke pattern raster
            bindProperty("stroke-opacity", &_strokeOpacity);
            bindProperty("stroke-linejoin", &_strokeLinejoin);
            bindProperty("stroke-linecap", &_strokeLinecap);
            bindProperty("stroke-dasharray", &_strokeDashArray);
            bindProperty("stroke-miterlimit", &_strokeMiterLimit, true); // shapes the joins at decode
            bindProperty("offset", &_offset);
            bindProperty("end-arrow", &_endArrow);
            bindProperty("arrow-width", &_arrowWidth);
            bindProperty("arrow-length", &_arrowLength);
            bindProperty("arrow-only", &_arrowOnly);
            bindProperty("arrow-path", &_arrowPath);
            bindProperty("arrow-scale", &_arrowScale);
            bindProperty("arrow-rotation", &_arrowRotation);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static std::shared_ptr<const std::vector<cglib::vec2<float>>> parseArrowPath(const std::string& path, float boxLength, float boxWidth, float scale, float rotation);
        static bool isConvexArrowPath(const std::vector<cglib::vec2<float>>& points);

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
        // An arrow head at the last vertex, sized in multiples of the line width. A casing rule
        // repeats the same three properties with its own width and gets an even border for free.
        BoolProperty _endArrow = BoolProperty(false);
        FloatProperty _arrowWidth = FloatProperty(3.0f);
        FloatProperty _arrowLength = FloatProperty(2.5f);
        BoolProperty _arrowOnly = BoolProperty(false);
        // A custom head outline as an SVG-style path (M/L/Z, absolute), in multiples of the line
        // width: x runs along the line, y across it. Empty means the built-in triangle.
        StringProperty _arrowPath = StringProperty("");
        // Applied to the fitted contour, about its centre. The scale multiplies what the box gave
        // it; the rotation is in degrees, clockwise on screen, and turns the head relative to the
        // direction of travel - an icon drawn pointing up needs 90.
        FloatProperty _arrowScale = FloatProperty(1.0f);
        FloatProperty _arrowRotation = FloatProperty(0.0f);

        ColorFunctionBuilder _strokeFuncBuilder;
    };
}

#endif
