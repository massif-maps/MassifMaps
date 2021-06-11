/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_POINTSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_POINTSYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace carto { namespace mvt {
    class PointSymbolizer : public GeometrySymbolizer {
    public:
        explicit PointSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            unbindParameter("geometry-transform"); // not supported for now
            bindParameter("file", &_file);
            bindParameter("opacity", &_opacity);
            bindParameter("allow-overlap", &_allowOverlap);
            bindParameter("ignore-placement", &_ignorePlacement);
            bindParameter("transform", &_transform);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        inline static constexpr int RECTANGLE_SIZE = 4;

        static std::shared_ptr<vt::BitmapImage> makeRectangleBitmap(float size);

        StringParameter _file;
        FloatFunctionParameter _opacity = FloatFunctionParameter(1.0f);
        BoolParameter _allowOverlap = BoolParameter(false);
        BoolParameter _ignorePlacement = BoolParameter(false);
        TransformParameter _transform;

        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _fillFuncBuilder;
    };
} }

#endif
