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
            bindProperty("file", &_file);
            bindProperty("marker-type", &_markerType);
            bindProperty("opacity", &_opacity);
            bindProperty("fill", &_fill);
            bindProperty("fill-opacity", &_fillOpacity);
            bindProperty("width", &_width);
            bindProperty("stroke", &_stroke);
            bindProperty("stroke-opacity", &_strokeOpacity);
            bindProperty("stroke-width", &_strokeWidth);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static constexpr int DEFAULT_MARKER_SIZE = 10;
        static constexpr int SUPERSAMPLING_FACTOR = 4;

        static std::shared_ptr<vt::BitmapImage> makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);
        static std::shared_ptr<vt::BitmapImage> makeRectangleBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);

        StringProperty _file;
        MarkerTypeProperty _markerType = MarkerTypeProperty("auto");
        FloatProperty _opacity = FloatProperty(1.0f);
        ColorProperty _fill = ColorProperty("#0000ff");
        FloatProperty _fillOpacity = FloatProperty(1.0f);
        FloatProperty _width = FloatProperty(10.0f);
        ColorProperty _stroke = ColorProperty("#000000");
        FloatProperty _strokeOpacity = FloatProperty(1.0f);
        FloatProperty _strokeWidth = FloatProperty(0.0f);

        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _fillFuncBuilder;
    };
}

#endif
