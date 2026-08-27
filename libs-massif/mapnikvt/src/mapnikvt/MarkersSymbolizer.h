/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_MARKERSSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_MARKERSSYMBOLIZER_H_

#include "Symbolizer.h"
#include "FunctionBuilder.h"

namespace massif::mvt {
    class MarkersSymbolizer : public Symbolizer {
    public:
        explicit MarkersSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            bindProperty("file", &_file);
            bindProperty("feature-id", &_featureId);
            bindProperty("placement", &_placement);
            bindProperty("marker-type", &_markerType);
            bindProperty("color", &_color);
            bindProperty("opacity", &_opacity);
            bindProperty("fill", &_fill);
            bindProperty("fill-opacity", &_fillOpacity);
            bindProperty("width", &_width, true); // sizes the generated marker bitmap
            bindProperty("height", &_height, true); // sizes the generated marker bitmap
            bindProperty("stroke", &_stroke);
            bindProperty("stroke-opacity", &_strokeOpacity);
            bindProperty("stroke-width", &_strokeWidth, true); // drawn into the generated bitmap
            bindProperty("spacing", &_spacing);
            bindProperty("placement-priority", &_placementPriority);
            bindProperty("rank", &_rank);
            bindProperty("max-distance", &_maxDistance);
            bindProperty("allow-overlap", &_allowOverlap);
            bindProperty("allow-overlap-same-feature-id", &_allowOverlapSameFeatureId);
            bindProperty("same-feature-id-dependent", &_sameFeatureIdDependent);
            bindProperty("clip", &_clip);
            bindProperty("ignore-placement", &_ignorePlacement);
            bindProperty("transform", &_transform);
            bindProperty("sdf", &_sdf);
            bindProperty("halo-fill", &_haloFill);
            bindProperty("halo-opacity", &_haloOpacity);
            bindProperty("halo-radius", &_haloRadius);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static constexpr int DEFAULT_CIRCLE_SIZE = 10;
        static constexpr int DEFAULT_ARROW_WIDTH = 28;
        static constexpr int DEFAULT_ARROW_HEIGHT = 14;
        static constexpr int SUPERSAMPLING_FACTOR = 4;
        static constexpr int MAX_BITMAP_SIZE = 64;
        static constexpr float IMAGE_UPSAMPLING_SCALE = 2.5f;

        static std::vector<std::pair<vt::Transform, vt::TileLayerBuilder::Vertices>> generateTransformedPoints(const vt::TileLayerBuilder::Vertices& vertices, float spacing, float bitmapSize, float tileSize);

        static std::shared_ptr<vt::BitmapImage> makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);
        static std::shared_ptr<vt::BitmapImage> makeArrowBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);

        StringProperty _file;
        // The image is a distance field rather than a picture - mapbox carries this per sprite
        // entry ("sdf": true), which a bare file cannot, so the style says it.
        BoolProperty _sdf = BoolProperty(false);
        ColorFunctionProperty _haloFill = ColorFunctionProperty("#ffffff");
        FloatFunctionProperty _haloOpacity = FloatFunctionProperty(1.0f);
        FloatFunctionProperty _haloRadius = FloatFunctionProperty(0.0f);
        ValueProperty _featureId;
        LabelOrientationProperty _placement = LabelOrientationProperty("point");
        MarkerTypeProperty _markerType = MarkerTypeProperty("auto");
        ColorFunctionProperty _color = ColorFunctionProperty("#ffffff");
        FloatFunctionProperty _opacity = FloatFunctionProperty(1.0f);
        ColorProperty _fill = ColorProperty("#0000ff");
        FloatProperty _fillOpacity = FloatProperty(1.0f);
        FloatFunctionProperty _width = FloatFunctionProperty(0.0f);
        FloatFunctionProperty _height = FloatFunctionProperty(0.0f);
        ColorProperty _stroke = ColorProperty("#000000");
        FloatProperty _strokeOpacity = FloatProperty(1.0f);
        FloatFunctionProperty _strokeWidth = FloatFunctionProperty(0.5f);
        FloatProperty _spacing = FloatProperty(100.0f);
        FloatProperty _placementPriority = FloatProperty(0.0f);
        // Added to placement-priority by the culler, once per label and per placement pass, so the
        // expression can read view::distance - see TextSymbolizer::_rank.
        FloatFunctionProperty _rank = FloatFunctionProperty(0.0f);
        FloatProperty _maxDistance = FloatProperty(0.0f); // meters from the camera; 0 = no limit
        BoolProperty _allowOverlap = BoolProperty(false);
        BoolProperty _clip = BoolProperty(false);
        BoolProperty _allowOverlapSameFeatureId = BoolProperty(false);
        BoolProperty _sameFeatureIdDependent = BoolProperty(false);
        BoolProperty _ignorePlacement = BoolProperty(false);
        TransformProperty _transform;

        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _fillFuncBuilder;
        ColorFunctionBuilder _haloFuncBuilder;
    };
}

#endif
