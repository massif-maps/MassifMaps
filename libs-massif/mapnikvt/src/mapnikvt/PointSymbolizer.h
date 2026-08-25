/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_POINTSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_POINTSYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace massif::mvt {
    class PointSymbolizer : public GeometrySymbolizer {
    public:
        explicit PointSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            unbindProperty("geometry-transform"); // not supported for now
            bindProperty("file", &_file);
            bindProperty("opacity", &_opacity);
            bindProperty("allow-overlap", nullptr); // a point is not placed, so nothing to allow
            bindProperty("ignore-placement", nullptr);
            bindProperty("transform", &_transform);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static constexpr int RECTANGLE_SIZE = 4;

        static std::shared_ptr<vt::BitmapImage> makeRectangleBitmap(float size);

        StringProperty _file;
        FloatFunctionProperty _opacity = FloatFunctionProperty(1.0f);
        TransformProperty _transform;

        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _fillFuncBuilder;
    };
}

#endif
