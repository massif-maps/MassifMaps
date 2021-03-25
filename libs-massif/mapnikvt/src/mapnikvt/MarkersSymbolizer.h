/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_MARKERSSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_MARKERSSYMBOLIZER_H_

#include "Symbolizer.h"

namespace carto { namespace mvt {
    class MarkersSymbolizer : public Symbolizer {
    public:
        explicit MarkersSymbolizer(std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)) {
            bind(&_strokeWidthFunc, Value(_strokeWidthStatic));
        }

        virtual void build(const FeatureCollection& featureCollection, const FeatureExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) override;

    protected:
        inline static constexpr int DEFAULT_CIRCLE_SIZE = 10;
        inline static constexpr int DEFAULT_ARROW_WIDTH = 28;
        inline static constexpr int DEFAULT_ARROW_HEIGHT = 14;
        inline static constexpr int SUPERSAMPLING_FACTOR = 4;
        inline static constexpr int MAX_BITMAP_SIZE = 64;
        inline static constexpr float IMAGE_UPSAMPLING_SCALE = 2.5f;

        virtual void bindParameter(const std::string& name, const std::string& value) override;

        static bool containsRotationTransform(const Value& val);

        static std::shared_ptr<vt::BitmapImage> makeEllipseBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);
        static std::shared_ptr<vt::BitmapImage> makeArrowBitmap(float width, float height, const vt::Color& color, float strokeWidth, const vt::Color& strokeColor);

        std::string _file;
        std::string _placement = "point";
        std::string _markerType;
        long long _featureId = 0;
        bool _featureIdDefined = false;
        vt::Color _fill = vt::Color(0xff0000ff);
        float _fillOpacity = 1.0f;
        vt::FloatFunction _widthFunc; // undefined
        float _widthStatic = 0;
        bool _widthDefined = false;
        vt::FloatFunction _heightFunc; // undefined
        float _heightStatic = 0;
        bool _heightDefined = false;
        vt::Color _stroke = vt::Color(0xff000000);
        float _strokeOpacity = 1.0f;
        vt::FloatFunction _strokeWidthFunc; // 0.5f
        float _strokeWidthStatic = 0.5f;
        float _spacing = 100.0f;
        float _placementPriority = 0.0f;
        bool _allowOverlap = false;
        bool _clip = false;
        bool _clipDefined = false;
        bool _ignorePlacement = false;
        vt::Transform _transform;
        std::optional<Expression> _transformExpression;
    };
} }

#endif
