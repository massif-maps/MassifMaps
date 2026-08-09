/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILELABEL_H_
#define _CARTO_VT_TILELABEL_H_

#include "Color.h"
#include "Transform.h"
#include "Bitmap.h"
#include "Font.h"
#include "ViewState.h"
#include "VertexArray.h"
#include "Styles.h"

#include <memory>
#include <optional>
#include <array>
#include <list>
#include <vector>
#include <limits>
#include <algorithm>

namespace carto::vt {
    class TileLabel final {
    public:
        struct Style {
            LabelOrientation orientation;
            ColorFunction colorFunc;
            FloatFunction sizeFunc;
            ColorFunction haloColorFunc;
            FloatFunction haloRadiusFunc;
            bool autoflip;
            float scale;
            float ascent;
            float descent;
            std::optional<Transform> transform;
            std::shared_ptr<const GlyphMap> glyphMap;
            int glyphRenderSize;
            // Meters from the camera beyond which the label is not placed at all; 0 = no limit.
            // A label glyph is screen-space, so a street name 5km away is drawn at the same size as
            // one 50m away, and a tilted view fills its horizon band with unreadable labels. Which
            // labels a tile carries is already decided by the style at the TILE's zoom, so this is
            // the second half of that: how far the ones that exist may be seen.
            float maxDistance;
            // Own colour for the second run of text (see TextLabelStyle); unset = the label's fill.
            std::optional<ColorFunction> secondaryColorFunc;
            // Own colour for the icon run (the glyphs before the text, see TextLabelStyle);
            // unset = the label's fill.
            std::optional<ColorFunction> iconColorFunc;
            // Added to the placement priority by the culler, per label and per pass - the one
            // place a style function may read view::distance (see TextLabelStyle::rankFunc).
            FloatFunction rankFunc;
            // CALLOUT orientation only - see LabelOrientation. Pixels, except the anchor, which is
            // a fraction of the screen height from the top (< 0 = stack from the label's own
            // anchor). lineGlyph is the atlas cell the leader line quad is textured from.
            float calloutScreenAnchor;
            float calloutOffset;
            float calloutStep;
            int calloutMaxRows;
            int calloutPersistPasses;
            float calloutLineWidth;
            // Normalized label-box points: (-1,-1) bottom left, (0,0) centre, (1,1) top right,
            // rotated with the text; unset = the label's own layout origin (see TextLabelStyle).
            std::optional<cglib::vec2<float>> calloutLineAnchor;
            std::optional<cglib::vec2<float>> calloutBandAnchor;
            std::optional<GlyphMap::Glyph> calloutLineGlyph;
            // A plate drawn behind part of the label (see LabelPlateStyle). 'glyph' is the atlas
            // cell it is 3-sliced from, so the corners keep their radius however wide the text is;
            // 'borderGlyph' is the same at radius + borderWidth, drawn behind the fill and one
            // border width larger on every side - which is what makes the border.
            struct Plate {
                LabelPlateStyle style;
                std::optional<GlyphMap::Glyph> glyph;
                std::optional<GlyphMap::Glyph> borderGlyph;

                bool drawsFill() const { return style.hasFill() && glyph.has_value(); }
                bool drawsBorder() const { return style.hasBorder() && borderGlyph.has_value(); }
                bool draws() const { return drawsFill() || drawsBorder(); }
                bool operator == (const Plate& other) const { return style == other.style; }
                bool operator != (const Plate& other) const { return !(*this == other); }
            };
            Plate textPlate; // behind the text
            Plate iconPlate; // behind the icon run
            // Justification of the text's lines for a label with no variants; a variant carries
            // its own (see Variant::lineAlign). -1 flush left, 0 centred, +1 flush right.
            float textLineAlign = 0.0f;

            explicit Style(LabelOrientation orientation, ColorFunction colorFunc, FloatFunction sizeFunc, ColorFunction haloColorFunc, FloatFunction haloRadiusFunc, bool autoflip, float scale, float ascent, float descent, const std::optional<Transform>& transform, std::shared_ptr<const GlyphMap> glyphMap, int glyphRenderSize, float maxDistance = 0.0f, const std::optional<ColorFunction>& secondaryColorFunc = std::optional<ColorFunction>(), FloatFunction rankFunc = FloatFunction(0.0f), float calloutScreenAnchor = -1.0f, float calloutOffset = 0.0f, float calloutStep = 0.0f, int calloutMaxRows = 8, int calloutPersistPasses = 0, float calloutLineWidth = 1.0f, const std::optional<cglib::vec2<float>>& calloutLineAnchor = std::optional<cglib::vec2<float>>(), const std::optional<cglib::vec2<float>>& calloutBandAnchor = std::optional<cglib::vec2<float>>(), const std::optional<GlyphMap::Glyph>& calloutLineGlyph = std::optional<GlyphMap::Glyph>(), const Plate& textPlate = Plate(), const Plate& iconPlate = Plate(), float textLineAlign = 0.0f, const std::optional<ColorFunction>& iconColorFunc = std::optional<ColorFunction>()) : orientation(orientation), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), haloColorFunc(std::move(haloColorFunc)), haloRadiusFunc(std::move(haloRadiusFunc)), autoflip(autoflip), scale(scale), ascent(ascent), descent(descent), transform(transform), glyphMap(std::move(glyphMap)), glyphRenderSize(glyphRenderSize), maxDistance(maxDistance), secondaryColorFunc(secondaryColorFunc), rankFunc(std::move(rankFunc)), calloutScreenAnchor(calloutScreenAnchor), calloutOffset(calloutOffset), calloutStep(calloutStep), calloutMaxRows(calloutMaxRows), calloutPersistPasses(calloutPersistPasses), calloutLineWidth(calloutLineWidth), calloutLineAnchor(calloutLineAnchor), calloutBandAnchor(calloutBandAnchor), calloutLineGlyph(calloutLineGlyph), textPlate(textPlate), iconPlate(iconPlate), textLineAlign(textLineAlign), iconColorFunc(iconColorFunc) { }
        };

        // One candidate layout of the label's TEXT (see TextLabelStyle::anchors). The icon glyphs
        // that come before the first line break are never moved, so a shield keeps its icon on the
        // feature whichever side the culler ends up putting the name on. An empty variant list is
        // the fixed layout every style had before the property existed.
        struct Variant {
            cglib::vec2<float> shift; // glyph units, added to the text pen
            bool drawText;            // false = the icon alone, the last resort of 'text-optional'
            // How the lines of a wrapped name are justified on THIS side: -1 flush left, 0 centred,
            // +1 flush right. It is the side's own value, so a two-line name is flush against the
            // icon whichever side it ends up on.
            float lineAlign = 0.0f;

            explicit Variant(const cglib::vec2<float>& shift, bool drawText, float lineAlign = 0.0f) : shift(shift), drawText(drawText), lineAlign(lineAlign) { }
        };

        struct PlacementInfo {
            int priority;
            float minimumGroupDistance;
            bool allowOverlapSameFeatureId;
            bool sameFeatureIdDependent;
            explicit PlacementInfo(int priority, float minimumGroupDistance,bool allowOverlapSameFeatureId, bool sameFeatureIdDependent) : priority(priority), minimumGroupDistance(minimumGroupDistance), allowOverlapSameFeatureId(allowOverlapSameFeatureId), sameFeatureIdDependent(sameFeatureIdDependent) { }
        };
        
        explicit TileLabel(long long localId, long long globalId, long long groupId, std::vector<Font::Glyph> glyphs, std::optional<cglib::vec2<float>> position, std::vector<cglib::vec2<float>> vertices, std::shared_ptr<const Style> style, const PlacementInfo& placementInfo, int geoPointIndex, std::vector<Variant> variants = std::vector<Variant>()) : _localId(localId), _globalId(globalId), _groupId(groupId), _glyphs(std::move(glyphs)), _position(std::move(position)), _vertices(std::move(vertices)), _style(std::move(style)), _placementInfo(placementInfo), _geoPointIndex(geoPointIndex), _variants(std::move(variants)) { }

        long long getLocalId() const { return _localId; }
        long long getGlobalId() const { return _globalId; }
        long long getGroupId() const { return _groupId; }
        long long getGeoPointIndex() const { return _geoPointIndex; }
        const std::vector<Font::Glyph>& getGlyphs() const { return _glyphs; }
        const std::optional<cglib::vec2<float>>& getPosition() const { return _position; }
        const std::vector<cglib::vec2<float>>& getVertices() const { return _vertices; }
        const std::shared_ptr<const Style>& getStyle() const { return _style; }
        const PlacementInfo& getPlacementInfo() const { return _placementInfo; }
        const std::vector<Variant>& getVariants() const { return _variants; }

        std::size_t getResidentSize() const {
            return 16 + sizeof(TileLabel);
        }

    private:
        const long long _localId;
        const long long _globalId;
        const long long _groupId;
        const int _geoPointIndex;
        const std::vector<Font::Glyph> _glyphs;
        const std::optional<cglib::vec2<float>> _position;
        const std::vector<cglib::vec2<float>> _vertices;
        const std::shared_ptr<const Style> _style;
        const PlacementInfo _placementInfo;
        const std::vector<Variant> _variants;
    };
}

#endif
