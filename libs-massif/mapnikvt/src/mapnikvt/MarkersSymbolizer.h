/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_MARKERSSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_MARKERSSYMBOLIZER_H_

#include "Symbolizer.h"
#include "FunctionBuilder.h"

namespace carto { namespace mvt {
    class MarkersSymbolizer : public Symbolizer {
    public:
        explicit MarkersSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            bindParameter("file", &_file);
            bindParameter("feature-id", &_featureId);
            bindParameter("placement", &_placement);
            bindParameter("marker-type", &_markerType);
            bindParameter("opacity", &_opacity);
            bindParameter("fill", &_fill);
            bindParameter("fill-opacity", &_fillOpacity);
            bindParameter("width", &_width);
            bindParameter("height", &_height);
            bindParameter("stroke", &_stroke);
            bindParameter("stroke-opacity", &_strokeOpacity);
            bindParameter("stroke-width", &_strokeWidth);
            bindParameter("spacing", &_spacing);
            bindParameter("placement-priority", &_placementPriority);
            bindParameter("allow-overlap", &_allowOverlap);
            bindParameter("clip", &_clip);
            bindParameter("ignore-placement", &_ignorePlacement);
            bindParameter("transform", &_transform);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        inline static constexpr int DEFAULT_CIRCLE_SIZE = 10;
        inline static constexpr int DEFAULT_ARROW_WIDTH = 28;
        inline static constexpr int DEFAULT_ARROW_HEIGHT = 14;
        inline static constexpr int SUPERSAMPLING_FACTOR = 4;
        inline static constexpr int MAX_BITMAP_SIZE = 64;
        inline static constexpr float IMAGE_UPSAMPLING_SCALE = 2.5f;

        static std::vector<std::pair<vt::Transform, vt::TileLayerBuilder::Vertices>> generateTransformedPoints(const vt::TileLayerBuilder::Vertices& vertices, float spacing, float bitmapSize, float tileSize);

        static std::shared_ptr<vt::BitmapImage> makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);
        static std::shared_ptr<vt::BitmapImage> makeArrowBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);

        StringParameter _file;
        ValueParameter _featureId;
        LabelOrientationParameter _placement = LabelOrientationParameter("point");
        StringParameter _markerType;
        FloatParameter _opacity = FloatParameter(1.0f);
        ColorParameter _fill = ColorParameter("#0000ff");
        FloatParameter _fillOpacity = FloatParameter(1.0f);
        FloatFunctionParameter _width = FloatFunctionParameter(0.0f);
        FloatFunctionParameter _height = FloatFunctionParameter(0.0f);
        ColorParameter _stroke = ColorParameter("#000000");
        FloatParameter _strokeOpacity = FloatParameter(1.0f);
        FloatFunctionParameter _strokeWidth = FloatFunctionParameter(0.5f);
        FloatParameter _spacing = FloatParameter(100.0f);
        FloatParameter _placementPriority = FloatParameter(0.0f);
        BoolParameter _allowOverlap = BoolParameter(false);
        BoolParameter _clip = BoolParameter(false);
        BoolParameter _ignorePlacement = BoolParameter(false);
        TransformParameter _transform;

        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _fillFuncBuilder;
    };
} }

#endif
