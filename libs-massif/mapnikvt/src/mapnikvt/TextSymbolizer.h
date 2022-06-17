/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TEXTSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_TEXTSYMBOLIZER_H_

#include "Symbolizer.h"
#include "FontSet.h"
#include "Expression.h"
#include "FunctionBuilder.h"

#include <vector>
#include <optional>
#include <functional>

namespace carto::mvt {
    class TextSymbolizer : public Symbolizer {
    public:
        explicit TextSymbolizer(const Expression& text, std::vector<std::shared_ptr<FontSet>> fontSets, std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)), _fontSets(std::move(fontSets)) {
            _text.setExpression(text);
            bindProperty("name", &_text);
            bindProperty("feature-id", &_featureId);
            bindProperty("text-transform", &_textTransform);
            bindProperty("face-name", &_faceName);
            bindProperty("fontset-name", &_fontSetName);
            bindProperty("placement", &_placement);
            bindProperty("size", &_size);
            bindProperty("spacing", &_spacing);
            bindProperty("fill", &_fill);
            bindProperty("opacity", &_opacity);
            bindProperty("halo-fill", &_haloFill);
            bindProperty("halo-opacity", &_haloOpacity);
            bindProperty("halo-radius", &_haloRadius);
            bindProperty("orientation", &_orientationAngle);
            bindProperty("dx", &_dx);
            bindProperty("dy", &_dy);
            bindProperty("placement-priority", &_placementPriority);
            bindProperty("minimum-distance", &_minimumDistance);
            bindProperty("allow-overlap", &_allowOverlap);
            bindProperty("clip", &_clip);
            bindProperty("wrap-character", &_wrapCharacter),
            bindProperty("wrap-width", &_wrapWidth);
            bindProperty("wrap-before", &_wrapBefore);
            bindProperty("character-spacing", &_characterSpacing);
            bindProperty("line-spacing", &_lineSpacing);
            bindProperty("horizontal-alignment", &_horizontalAlignment);
            bindProperty("vertical-alignment", &_verticalAlignment);
            bindProperty("avoid-edges", nullptr);
            bindProperty("halo-rasterizer", nullptr);
        }

        const Expression& getText() const { return _text.getExpression(); }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static std::vector<std::pair<float, vt::TileLayerBuilder::Vertices>> generateLinePoints(const vt::TileLayerBuilder::Vertices& vertices, float spacing, float textSize, float tileSize);

        static cglib::bbox2<float> calculateTextSize(const std::shared_ptr<const vt::Font>& font, const std::string& text, const vt::TextFormatter& formatter);

        vt::LabelOrientation getPlacement(const ExpressionContext& exprContext) const;
        std::string getTransformedText(const ExpressionContext& exprContext) const;
        std::shared_ptr<const vt::Font> getFont(const SymbolizerContext& symbolizerContext, const ExpressionContext& exprContext) const;
        vt::TextFormatter::Options getFormatterOptions(const SymbolizerContext& symbolizerContext, const ExpressionContext& exprContext) const;

        const std::vector<std::shared_ptr<FontSet>> _fontSets;

        StringProperty _text;
        ValueProperty _featureId;
        TextTransformProperty _textTransform = TextTransformProperty("none");
        StringProperty _faceName;
        StringProperty _fontSetName;
        LabelOrientationProperty _placement = LabelOrientationProperty("point");
        FloatFunctionProperty _size = FloatFunctionProperty(10.0f);
        FloatProperty _spacing = FloatProperty(0.0f);
        ColorFunctionProperty _fill = ColorFunctionProperty("#000000");
        FloatFunctionProperty _opacity = FloatFunctionProperty(1.0f);
        ColorFunctionProperty _haloFill = ColorFunctionProperty("#ffffff");
        FloatFunctionProperty _haloOpacity = FloatFunctionProperty(1.0f);
        FloatFunctionProperty _haloRadius = FloatFunctionProperty(0.0f);
        FloatProperty _orientationAngle = FloatProperty(0.0f);
        FloatProperty _dx = FloatProperty(0.0f);
        FloatProperty _dy = FloatProperty(0.0f);
        FloatProperty _placementPriority = FloatProperty(0.0f);
        FloatProperty _minimumDistance = FloatProperty(0.0f);
        BoolProperty _allowOverlap = BoolProperty(false);
        BoolProperty _clip = BoolProperty(false);
        StringProperty _wrapCharacter = StringProperty("");
        FloatProperty _wrapWidth = FloatProperty(0.0f);
        BoolProperty _wrapBefore = BoolProperty(false);
        FloatProperty _characterSpacing = FloatProperty(0.0f);
        FloatProperty _lineSpacing = FloatProperty(0.0f);
        HorizontalAlignmentProperty _horizontalAlignment = HorizontalAlignmentProperty("auto");
        VerticalAlignmentProperty _verticalAlignment = VerticalAlignmentProperty("auto");

        ColorFunctionBuilder _fillFuncBuilder;
        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _haloFillFuncBuilder;
        FloatFunctionBuilder _haloRadiusFuncBuilder;
    };
}

#endif
