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

namespace carto { namespace mvt {
    class TextSymbolizer : public Symbolizer {
    public:
        explicit TextSymbolizer(const Expression& text, std::vector<std::shared_ptr<FontSet>> fontSets, std::shared_ptr<Logger> logger) : Symbolizer(std::move(logger)), _fontSets(std::move(fontSets)) {
            _text.setExpression(text);
            bindParameter("name", &_text);
            bindParameter("feature-id", &_featureId);
            bindParameter("text-transform", &_textTransform);
            bindParameter("face-name", &_faceName);
            bindParameter("fontset-name", &_fontSetName);
            bindParameter("placement", &_placement);
            bindParameter("size", &_size);
            bindParameter("spacing", &_spacing);
            bindParameter("fill", &_fill);
            bindParameter("opacity", &_opacity);
            bindParameter("halo-fill", &_haloFill);
            bindParameter("halo-opacity", &_haloOpacity);
            bindParameter("halo-radius", &_haloRadius);
            bindParameter("orientation", &_orientationAngle);
            bindParameter("dx", &_dx);
            bindParameter("dy", &_dy);
            bindParameter("placement-priority", &_placementPriority);
            bindParameter("minimum-distance", &_minimumDistance);
            bindParameter("allow-overlap", &_allowOverlap);
            bindParameter("clip", &_clip);
            bindParameter("wrap-character", &_wrapCharacter),
            bindParameter("wrap-width", &_wrapWidth);
            bindParameter("wrap-before", &_wrapBefore);
            bindParameter("character-spacing", &_characterSpacing);
            bindParameter("line-spacing", &_lineSpacing);
            bindParameter("horizontal-alignment", &_horizontalAlignment);
            bindParameter("vertical-alignment", &_verticalAlignment);
            bindParameter("avoid-edges", nullptr);
            bindParameter("halo-rasterizer", nullptr);
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

        StringParameter _text;
        ValueParameter _featureId;
        StringParameter _textTransform;
        StringParameter _faceName;
        StringParameter _fontSetName;
        LabelOrientationParameter _placement = LabelOrientationParameter("point");
        FloatFunctionParameter _size = FloatFunctionParameter(10.0f);
        FloatParameter _spacing = FloatParameter(0.0f);
        ColorFunctionParameter _fill = ColorFunctionParameter("#000000");
        FloatFunctionParameter _opacity = FloatFunctionParameter(1.0f);
        ColorFunctionParameter _haloFill = ColorFunctionParameter("#ffffff");
        FloatFunctionParameter _haloOpacity = FloatFunctionParameter(1.0f);
        FloatFunctionParameter _haloRadius = FloatFunctionParameter(0.0f);
        FloatParameter _orientationAngle = FloatParameter(0.0f);
        FloatParameter _dx = FloatParameter(0.0f);
        FloatParameter _dy = FloatParameter(0.0f);
        FloatParameter _placementPriority = FloatParameter(0.0f);
        FloatParameter _minimumDistance = FloatParameter(0.0f);
        BoolParameter _allowOverlap = BoolParameter(false);
        BoolParameter _clip = BoolParameter(false);
        StringParameter _wrapCharacter = StringParameter("");
        FloatParameter _wrapWidth = FloatParameter(0.0f);
        BoolParameter _wrapBefore = BoolParameter(false);
        FloatParameter _characterSpacing = FloatParameter(0.0f);
        FloatParameter _lineSpacing = FloatParameter(0.0f);
        StringParameter _horizontalAlignment = StringParameter("auto");
        StringParameter _verticalAlignment = StringParameter("auto");

        ColorFunctionBuilder _fillFuncBuilder;
        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _haloFillFuncBuilder;
        FloatFunctionBuilder _haloRadiusFuncBuilder;
    };
} }

#endif
