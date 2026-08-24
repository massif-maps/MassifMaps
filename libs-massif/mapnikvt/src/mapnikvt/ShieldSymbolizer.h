/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_SHIELDSYMBOLIZER_H_
#define _MASSIF_MAPNIKVT_SHIELDSYMBOLIZER_H_

#include "TextSymbolizer.h"

namespace massif::mvt {
    class ShieldSymbolizer : public TextSymbolizer {
    public:
        explicit ShieldSymbolizer(const Expression& text, std::vector<std::shared_ptr<FontSet>> fontSets, std::shared_ptr<Logger> logger) : TextSymbolizer(text, std::move(fontSets), std::move(logger)) {
            // A shield is a road marker: repeated along its road and facing the camera, which is
            // what every style had to spell out by hand. 'point' is still there for the old default.
            _placement = LabelOrientationProperty("billboard-line-repeat");
            bindProperty("file", &_file);
            bindProperty("shield-dx", &_shieldDx);
            bindProperty("shield-dy", &_shieldDy);
            bindProperty("unlock-image", &_unlockImage);
            bindProperty("anchors", &_anchors);
            bindProperty("text-optional", &_textOptional);
            bindProperty("icon-name", &_iconText);
            bindProperty("icon-face-name", &_iconFaceName);
            bindProperty("icon-size", &_iconSize);
            bindProperty("icon-fill", &_iconFill);
            bindProperty("icon-opacity", &_iconOpacity);
            bindProperty("icon-dx", &_iconDx);
            bindProperty("icon-dy", &_iconDy);
            bindProperty("text-horizontal-alignment", &_textHorizontalAlignment);
            bindProperty("icon-background-fill", &_iconBackgroundFill);
            bindProperty("icon-background-opacity", &_iconBackgroundOpacity);
            bindProperty("icon-background-radius", &_iconBackgroundRadius);
            bindProperty("icon-background-padding-x", &_iconBackgroundPaddingX);
            bindProperty("icon-background-padding-y", &_iconBackgroundPaddingY);
            bindProperty("icon-background-border-fill", &_iconBackgroundBorderFill);
            bindProperty("icon-background-border-opacity", &_iconBackgroundBorderOpacity);
            bindProperty("icon-background-border-width", &_iconBackgroundBorderWidth);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static constexpr float IMAGE_UPSAMPLING_SCALE = 2.5f;

        // 'right, left, top, bottom' and the four corners, in preference order - the culler takes
        // the first side that is free (see vt::LabelAnchor). Empty is one fixed layout, which is
        // what every shield did before the property existed.
        static std::vector<vt::LabelAnchor> parseAnchors(const std::string& anchors);
        // 'left' / 'middle' / 'right' / 'auto' - see _textHorizontalAlignment.
        static vt::LabelLineAlign parseLineAlign(const std::string& align);
        // The icon run: the glyphs of 'icon-name' shaped from 'icon-face-name', scaled, centred on
        // the anchor and marked as the icon run so that they keep their place when the text moves.
        // The face is resolved as a FALLBACK of the label font, which is what puts its glyphs in
        // the label's own atlas (FontManagerFont::shapeGlyphs) - a font of its own has an atlas of
        // its own, and one label can only be drawn from one.
        std::vector<vt::Font::Glyph> buildIconGlyphs(const std::shared_ptr<const vt::Font>& font, const SymbolizerContext& symbolizerContext, const ExpressionContext& exprContext, float fontSize) const;

        StringProperty _file;
        BoolProperty _unlockImage = BoolProperty(false);
        FloatProperty _shieldDx = FloatProperty(0.0f);
        FloatProperty _shieldDy = FloatProperty(0.0f);
        StringProperty _anchors = StringProperty("");
        BoolProperty _textOptional = BoolProperty(false);
        StringProperty _iconText = StringProperty("");
        StringProperty _iconFaceName = StringProperty("");
        FloatProperty _iconSize = FloatProperty(0.0f); // pixels; 0 = the label's own size
        ColorFunctionProperty _iconFill = ColorFunctionProperty("#000000");
        FloatFunctionProperty _iconOpacity = FloatFunctionProperty(1.0f);
        FloatProperty _iconDx = FloatProperty(0.0f);
        FloatProperty _iconDy = FloatProperty(0.0f);
        // How the LINES of a wrapped name are justified inside the text block - 'left', 'middle',
        // 'right' or 'auto'. 'auto' follows the side the culler put the name on (see
        // shield-anchors): flush against the icon on either side, which is what makes a two-line
        // name look the same distance from it as a one-line one. Unset keeps every line centred,
        // which is what a label did before the property existed.
        StringProperty _textHorizontalAlignment = StringProperty("");
        // The plate behind the ICON, mirroring 'background-*' (which is the one behind the text).
        ColorProperty _iconBackgroundFill = ColorProperty("transparent");
        FloatProperty _iconBackgroundOpacity = FloatProperty(1.0f);
        FloatProperty _iconBackgroundRadius = FloatProperty(0.0f);
        FloatProperty _iconBackgroundPaddingX = FloatProperty(3.0f);
        FloatProperty _iconBackgroundPaddingY = FloatProperty(2.0f);
        ColorProperty _iconBackgroundBorderFill = ColorProperty("#000000");
        FloatProperty _iconBackgroundBorderOpacity = FloatProperty(1.0f);
        FloatProperty _iconBackgroundBorderWidth = FloatProperty(0.0f);

        ColorFunctionBuilder _iconFillFuncBuilder;
    };
}

#endif
