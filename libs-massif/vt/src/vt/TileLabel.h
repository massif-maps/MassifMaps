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
            // The plate drawn behind the text (see TextLabelStyle). backgroundGlyph is the atlas
            // cell it is 3-sliced from, so the corners keep their radius however wide the text is.
            Color backgroundColor;
            float backgroundRadius = 0.0f;
            cglib::vec2<float> backgroundPadding = cglib::vec2<float>(0, 0);
            std::optional<GlyphMap::Glyph> backgroundGlyph;

            explicit Style(LabelOrientation orientation, ColorFunction colorFunc, FloatFunction sizeFunc, ColorFunction haloColorFunc, FloatFunction haloRadiusFunc, bool autoflip, float scale, float ascent, float descent, const std::optional<Transform>& transform, std::shared_ptr<const GlyphMap> glyphMap, int glyphRenderSize, float maxDistance = 0.0f, const std::optional<ColorFunction>& secondaryColorFunc = std::optional<ColorFunction>(), FloatFunction rankFunc = FloatFunction(0.0f), float calloutScreenAnchor = -1.0f, float calloutOffset = 0.0f, float calloutStep = 0.0f, int calloutMaxRows = 8, int calloutPersistPasses = 0, float calloutLineWidth = 1.0f, const std::optional<cglib::vec2<float>>& calloutLineAnchor = std::optional<cglib::vec2<float>>(), const std::optional<cglib::vec2<float>>& calloutBandAnchor = std::optional<cglib::vec2<float>>(), const std::optional<GlyphMap::Glyph>& calloutLineGlyph = std::optional<GlyphMap::Glyph>(), const Color& backgroundColor = Color(), float backgroundRadius = 0.0f, const cglib::vec2<float>& backgroundPadding = cglib::vec2<float>(0, 0), const std::optional<GlyphMap::Glyph>& backgroundGlyph = std::optional<GlyphMap::Glyph>()) : orientation(orientation), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), haloColorFunc(std::move(haloColorFunc)), haloRadiusFunc(std::move(haloRadiusFunc)), autoflip(autoflip), scale(scale), ascent(ascent), descent(descent), transform(transform), glyphMap(std::move(glyphMap)), glyphRenderSize(glyphRenderSize), maxDistance(maxDistance), secondaryColorFunc(secondaryColorFunc), rankFunc(std::move(rankFunc)), calloutScreenAnchor(calloutScreenAnchor), calloutOffset(calloutOffset), calloutStep(calloutStep), calloutMaxRows(calloutMaxRows), calloutPersistPasses(calloutPersistPasses), calloutLineWidth(calloutLineWidth), calloutLineAnchor(calloutLineAnchor), calloutBandAnchor(calloutBandAnchor), calloutLineGlyph(calloutLineGlyph), backgroundColor(backgroundColor), backgroundRadius(backgroundRadius), backgroundPadding(backgroundPadding), backgroundGlyph(backgroundGlyph) { }
        };

        struct PlacementInfo {
            int priority;
            float minimumGroupDistance;
            bool allowOverlapSameFeatureId;
            bool sameFeatureIdDependent;
            explicit PlacementInfo(int priority, float minimumGroupDistance,bool allowOverlapSameFeatureId, bool sameFeatureIdDependent) : priority(priority), minimumGroupDistance(minimumGroupDistance), allowOverlapSameFeatureId(allowOverlapSameFeatureId), sameFeatureIdDependent(sameFeatureIdDependent) { }
        };
        
        explicit TileLabel(long long localId, long long globalId, long long groupId, std::vector<Font::Glyph> glyphs, std::optional<cglib::vec2<float>> position, std::vector<cglib::vec2<float>> vertices, std::shared_ptr<const Style> style, const PlacementInfo& placementInfo, int geoPointIndex) : _localId(localId), _globalId(globalId), _groupId(groupId), _glyphs(std::move(glyphs)), _position(std::move(position)), _vertices(std::move(vertices)), _style(std::move(style)), _placementInfo(placementInfo), _geoPointIndex(geoPointIndex) { }

        long long getLocalId() const { return _localId; }
        long long getGlobalId() const { return _globalId; }
        long long getGroupId() const { return _groupId; }
        long long getGeoPointIndex() const { return _geoPointIndex; }
        const std::vector<Font::Glyph>& getGlyphs() const { return _glyphs; }
        const std::optional<cglib::vec2<float>>& getPosition() const { return _position; }
        const std::vector<cglib::vec2<float>>& getVertices() const { return _vertices; }
        const std::shared_ptr<const Style>& getStyle() const { return _style; }
        const PlacementInfo& getPlacementInfo() const { return _placementInfo; }

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
    };
}

#endif
