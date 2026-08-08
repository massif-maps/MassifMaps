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
            bindProperty("max-distance", &_maxDistance);
            bindProperty("callout-screen-anchor", &_calloutScreenAnchor);
            bindProperty("callout-offset", &_calloutOffset);
            bindProperty("callout-step", &_calloutStep);
            bindProperty("callout-max-rows", &_calloutMaxRows);
            bindProperty("callout-persist", &_calloutPersist);
            bindProperty("callout-line-width", &_calloutLineWidth);
            bindProperty("callout-line-anchor", &_calloutLineAnchor);
            bindProperty("callout-align", &_calloutAlign);
            bindProperty("rank", &_rank);
            bindProperty("secondary-name", &_secondaryText);
            bindProperty("secondary-scale", &_secondaryScale);
            bindProperty("secondary-fill", &_secondaryFill);
            bindProperty("secondary-opacity", &_secondaryOpacity);
            bindProperty("secondary-dx", &_secondaryDx);
            bindProperty("secondary-dy", &_secondaryDy);
            bindProperty("background-fill", &_backgroundFill);
            bindProperty("background-opacity", &_backgroundOpacity);
            bindProperty("background-radius", &_backgroundRadius);
            bindProperty("background-padding-x", &_backgroundPaddingX);
            bindProperty("background-padding-y", &_backgroundPaddingY);
            bindProperty("allow-overlap", &_allowOverlap);
            bindProperty("allow-overlap-same-feature-id", &_allowOverlapSameFeatureId);
            bindProperty("same-feature-id-dependent", &_sameFeatureIdDependent);
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

        static bool segmentIntersectRectangle(double a_rectangleMinX, double a_rectangleMinY, double a_rectangleMaxX, double a_rectangleMaxY,
                                                       double a_p1x, double a_p1y, double a_p2x, double a_p2y);
        static std::vector<std::pair<float, vt::TileLayerBuilder::Vertices>> generateLinePoints(const vt::TileLayerBuilder::Vertices& vertices, float spacing, float textSize, float tileSize, bool applyAngle = true);

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
        FloatProperty _maxDistance = FloatProperty(0.0f); // meters from the camera; 0 = no limit
        // 'nuticallout' placement only (see vt::LabelOrientation::CALLOUT). Screen pixels, except
        // the anchor: a fraction of the screen height from the top, < 0 = stack from the anchor.
        FloatProperty _calloutScreenAnchor = FloatProperty(-1.0f);
        FloatProperty _calloutOffset = FloatProperty(0.0f);
        FloatProperty _calloutStep = FloatProperty(0.0f); // negative stacks the rows DOWNWARDS
        FloatProperty _calloutMaxRows = FloatProperty(8.0f);
        // Placement passes a callout already on screen may fail before it is hidden. A panning map
        // rebuilds its labels constantly; 0 (the default) hides a name the first pass it loses.
        FloatProperty _calloutPersist = FloatProperty(0.0f);
        FloatProperty _calloutLineWidth = FloatProperty(1.0f);
        // Which point of the label box the leader line ends at, and which one sits on the band
        // line - 'center', 'bottom-left', 'top-right', ... (see getLabelBoxAnchorTable). Rotated
        // with the text, so a tilted name can hang from its first letter.
        StringProperty _calloutLineAnchor = StringProperty("");
        StringProperty _calloutAlign = StringProperty("");
        // Added to placement-priority by the culler, once per label and per placement pass, so
        // that the expression can read view::distance: 'text-rank: [ele] - [view::distance]/50'
        // ranks summits by height and nearness. Ranking only - it never changes how a label looks.
        FloatFunctionProperty _rank = FloatFunctionProperty(0.0f);
        // A second run of text after the name, at its own size: an elevation, a road number. Same
        // label, same plate, same colour - only the size and the baseline differ.
        StringProperty _secondaryText = StringProperty("");
        FloatProperty _secondaryScale = FloatProperty(0.7f);
        // Own colour for the second run; undefined leaves it the same as the label's fill.
        ColorFunctionProperty _secondaryFill = ColorFunctionProperty("#000000");
        FloatFunctionProperty _secondaryOpacity = FloatFunctionProperty(1.0f);
        FloatProperty _secondaryDx = FloatProperty(0.0f); // gap between the runs, pixels
        FloatProperty _secondaryDy = FloatProperty(0.0f); // baseline shift of the second run, pixels (down is positive, like dy)
        // A filled plate behind the text, for any placement - the classic-map use is a label over
        // busy ground. Transparent (opacity 0) draws nothing, which is the default.
        ColorProperty _backgroundFill = ColorProperty("#ffffff");
        FloatProperty _backgroundOpacity = FloatProperty(0.0f);
        FloatProperty _backgroundRadius = FloatProperty(0.0f);
        FloatProperty _backgroundPaddingX = FloatProperty(3.0f);
        FloatProperty _backgroundPaddingY = FloatProperty(2.0f);
        BoolProperty _allowOverlap = BoolProperty(false);
        BoolProperty _clip = BoolProperty(false);
        BoolProperty _allowOverlapSameFeatureId = BoolProperty(false);
        BoolProperty _sameFeatureIdDependent = BoolProperty(false);
        StringProperty _wrapCharacter = StringProperty("");
        FloatProperty _wrapWidth = FloatProperty(0.0f);
        BoolProperty _wrapBefore = BoolProperty(false);
        FloatProperty _characterSpacing = FloatProperty(0.0f);
        FloatProperty _lineSpacing = FloatProperty(0.0f);
        HorizontalAlignmentProperty _horizontalAlignment = HorizontalAlignmentProperty("auto");
        VerticalAlignmentProperty _verticalAlignment = VerticalAlignmentProperty("auto");

        ColorFunctionBuilder _secondaryFillFuncBuilder;
        ColorFunctionBuilder _fillFuncBuilder;
        FloatFunctionBuilder _sizeFuncBuilder;
        ColorFunctionBuilder _haloFillFuncBuilder;
        FloatFunctionBuilder _haloRadiusFuncBuilder;
    };
}

#endif
