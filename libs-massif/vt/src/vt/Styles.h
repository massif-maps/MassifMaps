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
    
    // CALLOUT: a point label lifted from its anchor in screen space, joined back by a leader line; the
    // culler moves it until free instead of hiding it. LINE_BILLBOARD_REPEAT never reaches a label style.
    enum class LabelOrientation {
        BILLBOARD_2D, BILLBOARD_3D, LINE_BILLBOARD_3D, LINE_BILLBOARD_REPEAT, POINT, LINE, CALLOUT
    };

    // Which side of its anchor the text goes; a style may list several in preference order and the
    // culler takes the first free one, the icon staying put. Same set and order as tangram's LabelProperty::Anchor.
    enum class LabelAnchor {
        CENTER, TOP, BOTTOM, LEFT, RIGHT, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT
    };

    // In the label's own screen-aligned frame, y up like the glyph offsets.
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

    // Justification of a wrapped label's lines. AUTO follows the side the culler chose, flush against the icon.
    enum class LabelLineAlign {
        CENTER, LEFT, RIGHT, AUTO
    };

    enum class RasterFilterMode {
        NONE, NEAREST, BILINEAR, BICUBIC
    };

    enum class LineJoinMode {
        NONE, BEVEL, MITER, ROUND
    };

    /**
     * What the terrain does to a line. DRAPE follows the ground and may be baked into the drape; SPAN
     * (bridge) and UNDERGROUND (tunnel) take a straight chord between their ends and are never baked.
     * Per symbolizer, not per layer: bridge-ness is a feature attribute within one `road` layer.
     */
    enum class LineElevationMode {
        DRAPE, SPAN, UNDERGROUND
    };

    enum class LineCapMode {
        NONE, SQUARE, ROUND
    };

    // A rounded plate behind a label's text or icon, the border a second larger plate behind the fill.
    // Screen pixels. No colour and no border (the default) draws nothing.
    struct LabelPlateStyle final {
        Color color;
        float radius = 0.0f;
        cglib::vec2<float> padding = cglib::vec2<float>(0, 0);
        // A fixed outer size per axis, border included; 0 sizes that axis by the content.
        cglib::vec2<float> size = cglib::vec2<float>(0, 0);
        Color borderColor;
        float borderWidth = 0.0f;

        bool hasFill() const { return color.value() != 0; }
        bool hasBorder() const { return borderColor.value() != 0 && borderWidth > 0.0f; }
        bool enabled() const { return hasFill() || hasBorder(); }

        bool operator == (const LabelPlateStyle& other) const {
            return color == other.color && radius == other.radius && padding == other.padding && size == other.size && borderColor == other.borderColor && borderWidth == other.borderWidth;
        }
        bool operator != (const LabelPlateStyle& other) const { return !(*this == other); }
    };

    /**
     * The box a plate covers; shared by culler and geometry so they cannot disagree. 'contentBox' is in
     * glyph units, returned times 'boxScale'; 'pixelScale' converts pixels to those units.
     * 'borderWidth' is the plate's own, snapped to its cell.
     */
    inline cglib::bbox2<float> calculatePlateBox(const cglib::bbox2<float>& contentBox, const LabelPlateStyle& style, float borderWidth, float boxScale, float pixelScale) {
        float mins[2], maxs[2];
        for (int axis = 0; axis < 2; axis++) {
            float low = contentBox.min(axis) * boxScale, high = contentBox.max(axis) * boxScale;
            if (style.size(axis) > 0) {
                float half = style.size(axis) * pixelScale * 0.5f, centre = (low + high) * 0.5f;
                mins[axis] = centre - half;
                maxs[axis] = centre + half;
            } else {
                float grow = (style.padding(axis) + borderWidth) * pixelScale;
                mins[axis] = low - grow;
                maxs[axis] = high + grow;
            }
        }
        return cglib::bbox2<float>(cglib::vec2<float>(mins[0], mins[1]), cglib::vec2<float>(maxs[0], maxs[1]));
    }

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
        bool backgroundSdf = false; // see TextLabelStyle

        explicit TextStyle(CompOp compOp, ColorFunction colorFunc, FloatFunction sizeFunc, ColorFunction haloColorFunc, FloatFunction haloRadiusFunc, float angle, float backgroundScale, const cglib::vec2<float>& backgroundOffset, std::shared_ptr<const BitmapImage> backgroundImage) : compOp(compOp), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), haloColorFunc(std::move(haloColorFunc)), haloRadiusFunc(std::move(haloRadiusFunc)), angle(angle), backgroundScale(backgroundScale), backgroundOffset(backgroundOffset), backgroundImage(std::move(backgroundImage)) { }
    };

    struct LineStyle final {
        CompOp compOp;
        LineJoinMode joinMode;
        LineCapMode capMode;
        ColorFunction colorFunc;
        // Emitted rather than lit fraction of the colour, see PolygonStyle. 1 = as authored.
        FloatFunction emissiveFunc;
        FloatFunction widthFunc;
        FloatFunction offsetFunc;
        // mapbox's `line-gap-width`: an undrawn gap down the middle, cut out by lineFsh at no extra geometry.
        FloatFunction gapWidthFunc;
        // mapbox's `line-blur`: widens the antialias ramp on both edges, in pixels; 0 = the plain one-pixel ramp.
        FloatFunction blurFunc;
        // maplibre's `line-border-*`: a casing, pixels on each side, drawn from the same buffer one draw earlier.
        ColorFunction borderColorFunc;
        FloatFunction borderWidthFunc;
        float splitDotLimit;
        float miterDotLimit;
        std::shared_ptr<const BitmapPattern> strokePattern;
        std::optional<Transform> transform;
        // Arrow head at the last vertex, in multiples of the line width; drawn only when both are positive
        // (default 0). The line stops where the head starts; the head keeps its screen size.
        float endArrowWidth;
        float endArrowLength;
        // Draw the head only, so a style can paint it over the shaft in a later rule.
        bool endArrowOnly;
        // Custom head outline in line widths, x along the line, y across; null = built-in triangle.
        // A skeleton: drawn half a line width larger all round.
        std::shared_ptr<const std::vector<cglib::vec2<float>>> endArrowShape;

        // See LineElevationMode; default DRAPE.
        LineElevationMode elevationMode;

        bool hasEndArrow() const { return (endArrowWidth > 0 && endArrowLength > 0) || (endArrowShape && endArrowShape->size() >= 3); }

        explicit LineStyle(CompOp compOp, LineJoinMode joinMode, LineCapMode capMode, ColorFunction colorFunc, FloatFunction widthFunc, FloatFunction offsetFunc, float splitDotLimit, float miterDotLimit, std::shared_ptr<const BitmapPattern> strokePattern, const std::optional<Transform>& transform, float endArrowWidth = 0, float endArrowLength = 0, bool endArrowOnly = false, std::shared_ptr<const std::vector<cglib::vec2<float>>> endArrowShape = std::shared_ptr<const std::vector<cglib::vec2<float>>>(), FloatFunction gapWidthFunc = FloatFunction(0), FloatFunction blurFunc = FloatFunction(0), FloatFunction emissiveFunc = FloatFunction(1.0f), ColorFunction borderColorFunc = ColorFunction(), FloatFunction borderWidthFunc = FloatFunction(0), LineElevationMode elevationMode = LineElevationMode::DRAPE) : compOp(compOp), joinMode(joinMode), capMode(capMode), colorFunc(std::move(colorFunc)), emissiveFunc(std::move(emissiveFunc)), widthFunc(std::move(widthFunc)), offsetFunc(std::move(offsetFunc)), gapWidthFunc(std::move(gapWidthFunc)), blurFunc(std::move(blurFunc)), borderColorFunc(std::move(borderColorFunc)), borderWidthFunc(std::move(borderWidthFunc)), splitDotLimit(splitDotLimit), miterDotLimit(miterDotLimit), strokePattern(std::move(strokePattern)), transform(transform), endArrowWidth(endArrowWidth), endArrowLength(endArrowLength), endArrowOnly(endArrowOnly), endArrowShape(std::move(endArrowShape)), elevationMode(elevationMode) { }
    };

    struct PolygonStyle final {
        CompOp compOp;
        ColorFunction colorFunc;
        // Emitted rather than lit fraction of the colour (mapbox *-emissive-strength); default 1 = as authored.
        FloatFunction emissiveFunc;
        std::shared_ptr<const BitmapPattern> pattern;
        std::optional<Transform> transform;
        // A bridge bed is a polygon and must leave the ground with its deck.
        LineElevationMode elevationMode;

        explicit PolygonStyle(CompOp compOp, ColorFunction colorFunc, std::shared_ptr<const BitmapPattern> pattern, const std::optional<Transform>& transform, FloatFunction emissiveFunc = FloatFunction(1.0f), LineElevationMode elevationMode = LineElevationMode::DRAPE) : compOp(compOp), colorFunc(std::move(colorFunc)), emissiveFunc(std::move(emissiveFunc)), pattern(std::move(pattern)), transform(transform), elevationMode(elevationMode) { }
    };

    // How an extrusion is capped; the non-FLAT shapes come from the OSM roof:shape tag.
    enum class RoofShape {
        FLAT, PYRAMIDAL, GABLED
    };

    struct Polygon3DStyle final {
        ColorFunction colorFunc;
        std::optional<Transform> transform;
        RoofShape roofShape = RoofShape::FLAT;
        float roofHeight = 0.0f; // metres above the wall top; 0 leaves the roof flat whatever the shape
        // See LineElevationMode: a bridge deck (SPAN) measures min-height/height from its chord.
        LineElevationMode elevationMode = LineElevationMode::DRAPE;
        // building-emissive-strength for this rule; unset = the map's, not 0. For extrusions that are
        // not buildings, e.g. a bridge deck replacing a road casing.
        std::optional<FloatFunction> emissiveFunc;

        explicit Polygon3DStyle(ColorFunction colorFunc, const std::optional<Transform>& transform) : colorFunc(std::move(colorFunc)), transform(transform) { }
        explicit Polygon3DStyle(ColorFunction colorFunc, const std::optional<Transform>& transform, RoofShape roofShape, float roofHeight, LineElevationMode elevationMode = LineElevationMode::DRAPE, std::optional<FloatFunction> emissiveFunc = std::optional<FloatFunction>()) : colorFunc(std::move(colorFunc)), transform(transform), roofShape(roofShape), roofHeight(roofHeight), elevationMode(elevationMode), emissiveFunc(std::move(emissiveFunc)) { }
    };

    struct PointLabelStyle final {
        LabelOrientation orientation;
        ColorFunction colorFunc;
        FloatFunction sizeFunc;
        bool autoflip;
        std::shared_ptr<const BitmapImage> image;
        std::optional<Transform> transform;
        float maxDistance; // meters from the camera beyond which the label is not placed; 0 = no limit
        // Opacity kept while the anchor is hidden by 3D content (mapbox text-occlusion-opacity); unset = the layer default.
        std::optional<float> occlusionOpacity;
        // The image is an SDF in its red channel, drawn through the glyph path; a bare image file cannot say so itself.
        bool sdfMode = false;
        ColorFunction haloColorFunc; // sdfMode only
        FloatFunction haloRadiusFunc;
        // See TextLabelStyle::rankFunc.
        FloatFunction rankFunc = FloatFunction(0.0f);
        // Emitted rather than lit fraction (mapbox text-/icon-emissive-strength); default 1, legible at any hour.
        FloatFunction emissiveFunc = FloatFunction(1.0f);
        // The halo's own emissive; unset = the label's.
        std::optional<FloatFunction> haloEmissiveFunc;

        explicit PointLabelStyle(LabelOrientation orientation, ColorFunction colorFunc, FloatFunction sizeFunc, bool autoflip, std::shared_ptr<const BitmapImage> image, const std::optional<Transform>& transform, float maxDistance = 0.0f, bool sdfMode = false, ColorFunction haloColorFunc = ColorFunction(), FloatFunction haloRadiusFunc = FloatFunction()) : orientation(orientation), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), autoflip(autoflip), image(std::move(image)), transform(transform), maxDistance(maxDistance), sdfMode(sdfMode), haloColorFunc(std::move(haloColorFunc)), haloRadiusFunc(std::move(haloRadiusFunc)) { }
    };

    struct TextLabelStyle final {
        // mapbox text-padding / icon-padding: screen pixels around the box, for the collision test only.
        float collisionPadding = 0.0f;
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
        // The background image is an SDF: drawn through the glyph path, coloured by iconColorFunc.
        bool backgroundSdf = false;
        float maxDistance; // meters from the camera beyond which the label is not placed; 0 = no limit
        // Opacity kept while the anchor is hidden by 3D content (mapbox text-occlusion-opacity); unset = the layer default.
        std::optional<float> occlusionOpacity;
        // The second run of text may have its own colour (unset = the label's own fill).
        std::optional<ColorFunction> secondaryColorFunc;
        // Added to the placement priority by the culler per label per pass, so it can read view::distance.
        FloatFunction rankFunc;
        // CALLOUT orientation only, all in screen pixels except the anchor:
        float calloutScreenAnchor; // where the label band sits, as a fraction of the screen height from the top; < 0 stacks it from its own anchor instead
        bool calloutBandFollow = false; // band drops to just above the highest on-screen anchor
        float calloutOffset;       // minimum distance the label is lifted above its anchor
        float calloutStep;         // how much further the next stacking row is; negative stacks downwards (a band pinned to the top)
        int calloutMaxRows;        // how many rows may be tried before the label is hidden
        int calloutPersistPasses;  // placement passes a callout that is already on screen may fail before it is hidden
        float calloutLineWidth;    // leader line width, 0 draws no line
        // Points of the label box, normalized: (-1,-1) bottom left, (0,0) centre, (1,1) top right; rotated
        // with the glyphs. Unset keeps the text around its own anchor.
        std::optional<cglib::vec2<float>> calloutLineAnchor; // the point held over the anchor, where the leader line ends
        std::optional<cglib::vec2<float>> calloutBandAnchor; // the point put on the band line (unset = the bottom of the box)
        LabelLineAlign textLineAlign = LabelLineAlign::CENTER;
        // Plates, any orientation; see LabelPlateStyle.
        LabelPlateStyle textPlate; // behind the text (the glyphs after the first line break)
        LabelPlateStyle iconPlate; // behind the icon run, which stays on the anchor
        // Sides for the text, in preference order (empty = one fixed layout). The text goes against the
        // icon's edge with dx/dy mirrored per side, unless textRadialOffset is set.
        std::vector<LabelAnchor> anchors;
        // mapbox 'text-radial-offset': pixels from the anchor to the text's near edge along the side's axis;
        // replaces dx/dy and the icon edge when set.
        float textRadialOffset = 0.0f;
        // mapbox 'text-optional': draw the icon alone when no side is free.
        bool textOptional;
        // Glyphs drawn before the text and not moved by the anchor: a shield bitmap or a font icon run.
        std::vector<Font::Glyph> iconGlyphs;
        // Own colour for the icon run; unset leaves it the label's fill.
        std::optional<ColorFunction> iconColorFunc;
        // mapbox icon-halo-*; unset draws none. Never the text's: an icon SDF has too few texels outside the ink.
        std::optional<ColorFunction> iconHaloColorFunc;
        std::optional<FloatFunction> iconHaloRadiusFunc;
        // The icon's own size ramp and the value it was baked at; the draw re-scales by their ratio.
        std::optional<FloatFunction> iconScaleFunc;
        float iconRefScale = 0.0f;
        // mapbox icon-opacity, evaluated live so the icon plate fades with its glyph.
        std::optional<FloatFunction> iconOpacityFunc;
        // Emitted rather than lit fraction (mapbox text-/icon-emissive-strength); default 1, legible at any hour.
        FloatFunction emissiveFunc = FloatFunction(1.0f);
        // The halo's own emissive; unset = the label's.
        std::optional<FloatFunction> haloEmissiveFunc;

        explicit TextLabelStyle(LabelOrientation orientation, ColorFunction colorFunc, FloatFunction sizeFunc, ColorFunction haloColorFunc, FloatFunction haloRadiusFunc, bool autoflip, float angle, float backgroundScale, const cglib::vec2<float>& backgroundOffset, std::shared_ptr<const BitmapImage> backgroundImage, float maxDistance = 0.0f, const std::optional<ColorFunction>& secondaryColorFunc = std::optional<ColorFunction>(), FloatFunction rankFunc = FloatFunction(0.0f), float calloutScreenAnchor = -1.0f, float calloutOffset = 0.0f, float calloutStep = 0.0f, int calloutMaxRows = 8, int calloutPersistPasses = 0, float calloutLineWidth = 1.0f, const std::optional<cglib::vec2<float>>& calloutLineAnchor = std::optional<cglib::vec2<float>>(), const std::optional<cglib::vec2<float>>& calloutBandAnchor = std::optional<cglib::vec2<float>>(), const LabelPlateStyle& textPlate = LabelPlateStyle(), const LabelPlateStyle& iconPlate = LabelPlateStyle(), LabelLineAlign textLineAlign = LabelLineAlign::CENTER, std::vector<LabelAnchor> anchors = std::vector<LabelAnchor>(), bool textOptional = false, std::vector<Font::Glyph> iconGlyphs = std::vector<Font::Glyph>(), const std::optional<ColorFunction>& iconColorFunc = std::optional<ColorFunction>()) : orientation(orientation), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), haloColorFunc(std::move(haloColorFunc)), haloRadiusFunc(std::move(haloRadiusFunc)), autoflip(autoflip), angle(angle), backgroundScale(backgroundScale), backgroundOffset(backgroundOffset), backgroundImage(std::move(backgroundImage)), maxDistance(maxDistance), secondaryColorFunc(secondaryColorFunc), rankFunc(std::move(rankFunc)), calloutScreenAnchor(calloutScreenAnchor), calloutOffset(calloutOffset), calloutStep(calloutStep), calloutMaxRows(calloutMaxRows), calloutPersistPasses(calloutPersistPasses), calloutLineWidth(calloutLineWidth), calloutLineAnchor(calloutLineAnchor), calloutBandAnchor(calloutBandAnchor), textPlate(textPlate), iconPlate(iconPlate), textLineAlign(textLineAlign), anchors(std::move(anchors)), textOptional(textOptional), iconGlyphs(std::move(iconGlyphs)), iconColorFunc(iconColorFunc) { }
    };
}

#endif
