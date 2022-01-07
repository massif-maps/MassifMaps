/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TORQUEMARKERSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_TORQUEMARKERSYMBOLIZER_H_

#include "Symbolizer.h"
#include "FunctionBuilder.h"

namespace carto::mvt {
    class TorqueMarkerSymbolizer : public Symbolizer {
    public:
        explicit TorqueMarkerSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            bindParameter("file", &_file);
            bindParameter("marker-type", &_markerType);
            bindParameter("opacity", &_opacity);
            bindParameter("fill", &_fill);
            bindParameter("fill-opacity", &_fillOpacity);
            bindParameter("width", &_width);
            bindParameter("stroke", &_stroke);
            bindParameter("stroke-opacity", &_strokeOpacity);
            bindParameter("stroke-width", &_strokeWidth);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static constexpr int DEFAULT_MARKER_SIZE = 10;
        static constexpr int SUPERSAMPLING_FACTOR = 4;

        static std::shared_ptr<vt::BitmapImage> makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);
        static std::shared_ptr<vt::BitmapImage> makeRectangleBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);

        StringParameter _file;
        MarkerTypeParameter _markerType = MarkerTypeParameter("auto");
        FloatParameter _opacity = FloatParameter(1.0f);
        ColorParameter _fill = ColorParameter("#0000ff");
        FloatParameter _fillOpacity = FloatParameter(1.0f);
        FloatParameter _width = FloatParameter(10.0f);
        ColorParameter _stroke = ColorParameter("#000000");
        FloatParameter _strokeOpacity = FloatParameter(1.0f);
        FloatParameter _strokeWidth = FloatParameter(0.0f);

        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _fillFuncBuilder;
    };
}

#endif
