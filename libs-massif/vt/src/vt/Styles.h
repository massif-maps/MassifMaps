/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_STYLES_H_
#define _MASSIF_VT_STYLES_H_

#include "Color.h"
#include "Transform.h"
#include "Bitmap.h"
#include "Font.h"
#include "ViewState.h"
#include "StrokeMap.h"
#include "GlyphMap.h"
#include "TextFormatter.h"
#include "UnaryFunction.h"

#include <optional>
#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif::vt {
    using FloatFunction = UnaryFunction<float, ViewState>;
    using ColorFunction = UnaryFunction<Color, ViewState>;

    enum class CompOp {
        SRC, SRC_OVER, SRC_IN, SRC_ATOP, 
        DST, DST_OVER, DST_IN, DST_ATOP,
        ZERO, PLUS, MINUS, MULTIPLY, SCREEN,
        DARKEN, LIGHTEN
    };
    
    // CALLOUT is a point label lifted away from its anchor in SCREEN space and joined back to it
    // by a leader line: the culler moves it until it is free instead of hiding it, so a dense set
    // of point features (summits in a panorama) is read as a stack of named lines rather than as
    // whichever few labels happened to win.
    enum class LabelOrientation {
        BILLBOARD_2D, BILLBOARD_3D, LINE_BILLBOARD_3D, POINT, LINE, CALLOUT
    };

    // Which side of its anchor a label's text is laid out on. A style may name SEVERAL of them, in
    // preference order, and the culler then takes the first one that is free (see
    // LabelCuller::placeAnchoredLabel) - the icon of a shield stays put and only its text moves.
    // Same set and same order as tangram's LabelProperty::Anchor.
    enum class LabelAnchor {
        CENTER, TOP, BOTTOM, LEFT, RIGHT, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT
    };

    // Which way the text is moved for an anchor, in x and y of the label's own (screen aligned)
    // frame. y is UP, like the glyph offsets.
    inline cglib::vec2<float> labelAnchorDirection(LabelAnchor anchor) {
        switch (anchor) {
        case LabelAnchor::TOP:          return cglib::vec2<float>( 0,  1);
        case LabelAnchor::BOTTOM:       return cglib::vec2<float>( 0, -1);
        case LabelAnchor::LEFT:         return cglib::vec2<float>(-1,  0);
        case LabelAnchor::RIGHT:        return cglib::vec2<float>( 1,  0);
        case LabelAnchor::TOP_LEFT:     return cglib::vec2<float>(-1,  1);
        case LabelAnchor::TOP_RIGHT:    return cglib::vec2<float>( 1,  1);
        case LabelAnchor::BOTTOM_LEFT:  return cglib::vec2<float>(-1, -1);
        case LabelAnchor::BOTTOM_RIGHT: return cglib::vec2<float>( 1, -1);
        default:                        return cglib::vec2<float>( 0,  0);
        }
    }

    // How the LINES of a wrapped label are justified inside its text block. AUTO follows the side
    // the culler put the text on (see LabelAnchor): flush against the icon on either side, which is
    // what makes a two-line name sit the same distance from the icon as a one-line one. CENTER is
    // what every label did before the property existed.
    enum class LabelLineAlign {
        CENTER, LEFT, RIGHT, AUTO
    };

    enum class RasterFilterMode {
        NONE, NEAREST, BILINEAR, BICUBIC
    };

    enum class LineJoinMode {
        NONE, BEVEL, MITER, ROUND
    };

    enum class LineCapMode {
        NONE, SQUARE, ROUND
    };

    // A filled plate behind a label's text, or behind its icon: a rounded rectangle sized to what
    // it sits behind, plus a border drawn as a second, larger plate behind the fill. Everything is
    // in screen pixels. A plate with no colour and no border draws nothing, which is the default.
    struct LabelPlateStyle final {
        Color color;
        float radius = 0.0f;
        cglib::vec2<float> padding = cglib::vec2<float>(0, 0);
        Color borderColor;
        float borderWidth = 0.0f;

        bool hasFill() const { return color.value() != 0; }
        bool hasBorder() const { return borderColor.value() != 0 && borderWidth > 0.0f; }
        bool enabled() const { return hasFill() || hasBorder(); }

        bool operator == (const LabelPlateStyle& other) const {
            return color == other.color && radius == other.radius && padding == other.padding && borderColor == other.borderColor && borderWidth == other.borderWidth;
        }
        bool operator != (const LabelPlateStyle& other) const { return !(*this == other); }
    };

    struct PointStyle final {
        CompOp compOp;
        ColorFunction colorFunc;
        FloatFunction sizeFunc;
        std::shared_ptr<const BitmapImage> image;
        std::optional<Transform> transform;

        explicit PointStyle(CompOp compOp, ColorFunction colorFunc, FloatFunction sizeFunc, std::shared_ptr<const BitmapImage> image, const std::optional<Transform>& transform) : compOp(compOp), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), image(std::move(image)), transform(transform) { }
    };

    struct TextStyle final {
        CompOp compOp;
        ColorFunction colorFunc;
        FloatFunction sizeFunc;
        ColorFunction haloColorFunc;
        FloatFunction haloRadiusFunc;
        float angle;
        float backgroundScale;
        cglib::vec2<float> backgroundOffset;
        std::shared_ptr<const BitmapImage> backgroundImage;

        explicit TextStyle(CompOp compOp, ColorFunction colorFunc, FloatFunction sizeFunc, ColorFunction haloColorFunc, FloatFunction haloRadiusFunc, float angle, float backgroundScale, const cglib::vec2<float>& backgroundOffset, std::shared_ptr<const BitmapImage> backgroundImage) : compOp(compOp), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), haloColorFunc(std::move(haloColorFunc)), haloRadiusFunc(std::move(haloRadiusFunc)), angle(angle), backgroundScale(backgroundScale), backgroundOffset(backgroundOffset), backgroundImage(std::move(backgroundImage)) { }
    };

    struct LineStyle final {
        CompOp compOp;
        LineJoinMode joinMode;
        LineCapMode capMode;
        ColorFunction colorFunc;
        FloatFunction widthFunc;
        FloatFunction offsetFunc;
        float splitDotLimit;
        float miterDotLimit;
        std::shared_ptr<const BitmapPattern> strokePattern;
        std::optional<Transform> transform;
        // An arrow head at the last vertex, in multiples of the line width - a route's maneuver
        // arrow, a one-way marker. Both must be positive for the head to be drawn. The line stops
        // where the head starts, and the head is extruded like the line itself, so it keeps its
        // screen size and a casing rule of the same shape produces an even border round the whole
        // arrow. 0 (the default) leaves the line's own cap alone.
        float endArrowWidth;
        float endArrowLength;
        // Draw the head and NOT the line, so a style can paint the head over the shaft instead of
        // under it: shaft rules first, head rules after. Where the head overlaps its own shaft -
        // a U-turn, a hairpin, anything tight enough at the zoom being looked at - the head keeps
        // its outline, which is what tells it apart from the line it sits on.
        bool endArrowOnly;
        // A custom head outline, in the same multiples of the line width as the two sizes above:
        // x runs along the line, y across it. Null means the built-in triangle. The contour is a
        // SKELETON - what is drawn is half a line width larger all round, so a casing rule using
        // the same path lands (casing - fill) / 2 outside the fill, as it does along the shaft.
        std::shared_ptr<const std::vector<cglib::vec2<float>>> endArrowShape;

        bool hasEndArrow() const { return (endArrowWidth > 0 && endArrowLength > 0) || (endArrowShape && endArrowShape->size() >= 3); }

        explicit LineStyle(CompOp compOp, LineJoinMode joinMode, LineCapMode capMode, ColorFunction colorFunc, FloatFunction widthFunc, FloatFunction offsetFunc, float splitDotLimit, float miterDotLimit, std::shared_ptr<const BitmapPattern> strokePattern, const std::optional<Transform>& transform, float endArrowWidth = 0, float endArrowLength = 0, bool endArrowOnly = false, std::shared_ptr<const std::vector<cglib::vec2<float>>> endArrowShape = std::shared_ptr<const std::vector<cglib::vec2<float>>>()) : compOp(compOp), joinMode(joinMode), capMode(capMode), colorFunc(std::move(colorFunc)), widthFunc(std::move(widthFunc)), offsetFunc(std::move(offsetFunc)), splitDotLimit(splitDotLimit), miterDotLimit(miterDotLimit), strokePattern(std::move(strokePattern)), transform(transform), endArrowWidth(endArrowWidth), endArrowLength(endArrowLength), endArrowOnly(endArrowOnly), endArrowShape(std::move(endArrowShape)) { }
    };

    struct PolygonStyle final {
        CompOp compOp;
        ColorFunction colorFunc;
        std::shared_ptr<const BitmapPattern> pattern;
        std::optional<Transform> transform;

        explicit PolygonStyle(CompOp compOp, ColorFunction colorFunc, std::shared_ptr<const BitmapPattern> pattern, const std::optional<Transform>& transform) : compOp(compOp), colorFunc(std::move(colorFunc)), pattern(std::move(pattern)), transform(transform) { }
    };

    struct Polygon3DStyle final {
        ColorFunction colorFunc;
        std::optional<Transform> transform;

        explicit Polygon3DStyle(ColorFunction colorFunc, const std::optional<Transform>& transform) : colorFunc(std::move(colorFunc)), transform(transform) { }
    };

    struct PointLabelStyle final {
        LabelOrientation orientation;
        ColorFunction colorFunc;
        FloatFunction sizeFunc;
        bool autoflip;
        std::shared_ptr<const BitmapImage> image;
        std::optional<Transform> transform;
        float maxDistance; // meters from the camera beyond which the label is not placed; 0 = no limit

        explicit PointLabelStyle(LabelOrientation orientation, ColorFunction colorFunc, FloatFunction sizeFunc, bool autoflip, std::shared_ptr<const BitmapImage> image, const std::optional<Transform>& transform, float maxDistance = 0.0f) : orientation(orientation), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), autoflip(autoflip), image(std::move(image)), transform(transform), maxDistance(maxDistance) { }
    };

    struct TextLabelStyle final {
        LabelOrientation orientation;
        ColorFunction colorFunc;
        FloatFunction sizeFunc;
        ColorFunction haloColorFunc;
        FloatFunction haloRadiusFunc;
        bool autoflip;
        float angle;
        float backgroundScale;
        cglib::vec2<float> backgroundOffset;
        std::shared_ptr<const BitmapImage> backgroundImage;
        float maxDistance; // meters from the camera beyond which the label is not placed; 0 = no limit
        // The second run of text may have its own colour (unset = the label's own fill).
        std::optional<ColorFunction> secondaryColorFunc;
        // Added to the label's placement priority by the culler, once per pass and per label, so
        // the expression behind it can read view::distance. Ranking, not appearance: it decides
        // which of two colliding labels keeps the slot.
        FloatFunction rankFunc;
        // CALLOUT orientation only, all in screen pixels except the anchor:
        float calloutScreenAnchor; // where the label band sits, as a fraction of the screen height from the top; < 0 stacks it from its own anchor instead
        float calloutOffset;       // minimum distance the label is lifted above its anchor
        float calloutStep;         // how much further the next stacking row is; NEGATIVE stacks downwards, which is what a band pinned to the top of the screen needs
        int calloutMaxRows;        // how many rows may be tried before the label is hidden
        int calloutPersistPasses;  // placement passes a callout that is already on screen may fail before it is hidden
        float calloutLineWidth;    // leader line width, 0 draws no line
        // Points OF THE LABEL BOX, in normalized box coordinates: (-1,-1) is the bottom left
        // corner of the text (plate included), (0,0) its centre, (1,1) the top right. Rotation
        // applies to them like it does to the glyphs, so on a tilted name the bottom left corner
        // is the start of the first letter. Unset keeps the text laid out around its own anchor.
        std::optional<cglib::vec2<float>> calloutLineAnchor; // the point held over the anchor, where the leader line ends
        std::optional<cglib::vec2<float>> calloutBandAnchor; // the point put on the band line (unset = the bottom of the box)
        // Plates behind the label, sized to what they sit behind. Any orientation, not just
        // CALLOUT - a classic map label reads over busy ground with one too. Alpha 0 draws nothing.
        LabelLineAlign textLineAlign = LabelLineAlign::CENTER;
        LabelPlateStyle textPlate; // behind the text (the glyphs after the first line break)
        LabelPlateStyle iconPlate; // behind the icon run, which stays on the anchor
        // Sides the text may be laid out on, in preference order (empty = one fixed layout, which
        // is what every style without the property gets). The icon does not move; the text is
        // placed against the icon's edge on the chosen side, and dx/dy are MIRRORED with it, so an
        // offset that pushes the text away from the icon on one side does so on all of them.
        std::vector<LabelAnchor> anchors;
        // A last resort of drawing the icon alone when no side is free, rather than dropping the
        // whole label (mapbox 'text-optional'). Needs an icon to be of any use.
        bool textOptional;
        // Glyphs drawn BEFORE the text and not moved by the anchor: the shield bitmap is one of
        // them (added by the tile builder), and so is a font icon - a run shaped from an icon face,
        // which shares the label font's atlas through the font fallback chain.
        std::vector<Font::Glyph> iconGlyphs;
        // Own colour for the icon run; unset leaves it the label's fill.
        std::optional<ColorFunction> iconColorFunc;

        explicit TextLabelStyle(LabelOrientation orientation, ColorFunction colorFunc, FloatFunction sizeFunc, ColorFunction haloColorFunc, FloatFunction haloRadiusFunc, bool autoflip, float angle, float backgroundScale, const cglib::vec2<float>& backgroundOffset, std::shared_ptr<const BitmapImage> backgroundImage, float maxDistance = 0.0f, const std::optional<ColorFunction>& secondaryColorFunc = std::optional<ColorFunction>(), FloatFunction rankFunc = FloatFunction(0.0f), float calloutScreenAnchor = -1.0f, float calloutOffset = 0.0f, float calloutStep = 0.0f, int calloutMaxRows = 8, int calloutPersistPasses = 0, float calloutLineWidth = 1.0f, const std::optional<cglib::vec2<float>>& calloutLineAnchor = std::optional<cglib::vec2<float>>(), const std::optional<cglib::vec2<float>>& calloutBandAnchor = std::optional<cglib::vec2<float>>(), const LabelPlateStyle& textPlate = LabelPlateStyle(), const LabelPlateStyle& iconPlate = LabelPlateStyle(), LabelLineAlign textLineAlign = LabelLineAlign::CENTER, std::vector<LabelAnchor> anchors = std::vector<LabelAnchor>(), bool textOptional = false, std::vector<Font::Glyph> iconGlyphs = std::vector<Font::Glyph>(), const std::optional<ColorFunction>& iconColorFunc = std::optional<ColorFunction>()) : orientation(orientation), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), haloColorFunc(std::move(haloColorFunc)), haloRadiusFunc(std::move(haloRadiusFunc)), autoflip(autoflip), angle(angle), backgroundScale(backgroundScale), backgroundOffset(backgroundOffset), backgroundImage(std::move(backgroundImage)), maxDistance(maxDistance), secondaryColorFunc(secondaryColorFunc), rankFunc(std::move(rankFunc)), calloutScreenAnchor(calloutScreenAnchor), calloutOffset(calloutOffset), calloutStep(calloutStep), calloutMaxRows(calloutMaxRows), calloutPersistPasses(calloutPersistPasses), calloutLineWidth(calloutLineWidth), calloutLineAnchor(calloutLineAnchor), calloutBandAnchor(calloutBandAnchor), textPlate(textPlate), iconPlate(iconPlate), textLineAlign(textLineAlign), anchors(std::move(anchors)), textOptional(textOptional), iconGlyphs(std::move(iconGlyphs)), iconColorFunc(iconColorFunc) { }
    };
}

#endif
