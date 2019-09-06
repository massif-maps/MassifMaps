/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILELABEL_H_
#define _CARTO_VT_TILELABEL_H_

#include "TileId.h"
#include "Color.h"
#include "Bitmap.h"
#include "Font.h"
#include "ViewState.h"
#include "VertexArray.h"
#include "Styles.h"

#include <memory>
#include <array>
#include <list>
#include <vector>
#include <limits>
#include <algorithm>

#include <boost/variant.hpp>
#include <boost/optional.hpp>

namespace carto { namespace vt {
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
            boost::optional<cglib::mat2x2<float>> transform;
            boost::optional<cglib::vec2<float>> translate;
            std::shared_ptr<const GlyphMap> glyphMap;

            explicit Style(LabelOrientation orientation, ColorFunction colorFunc, FloatFunction sizeFunc, ColorFunction haloColorFunc, FloatFunction haloRadiusFunc, bool autoflip, float scale, float ascent, float descent, const boost::optional<cglib::mat2x2<float>>& transform, const boost::optional<cglib::vec2<float>>& translate, std::shared_ptr<const GlyphMap> glyphMap) : orientation(orientation), colorFunc(std::move(colorFunc)), sizeFunc(std::move(sizeFunc)), haloColorFunc(std::move(haloColorFunc)), haloRadiusFunc(std::move(haloRadiusFunc)), autoflip(autoflip), scale(scale), ascent(ascent), descent(descent), transform(transform), translate(translate), glyphMap(std::move(glyphMap)) { }
        };
        
        explicit TileLabel(const TileId& tileId, long long localId, long long globalId, long long groupId, std::vector<Font::Glyph> glyphs, boost::optional<cglib::vec2<float>> position, std::vector<cglib::vec2<float>> vertices, std::shared_ptr<const Style> style) : _tileId(tileId), _localId(localId), _globalId(globalId), _groupId(groupId), _glyphs(std::move(glyphs)), _position(std::move(position)), _vertices(std::move(vertices)), _style(std::move(style)) { }

        const TileId& getTileId() const { return _tileId; }
        long long getLocalId() const { return _localId; }
        long long getGlobalId() const { return _globalId; }
        long long getGroupId() const { return _groupId; }
        const std::vector<Font::Glyph>& getGlyphs() const { return _glyphs; }
        const boost::optional<cglib::vec2<float>>& getPosition() const { return _position; }
        const std::vector<cglib::vec2<float>>& getVertices() const { return _vertices; }
        const std::shared_ptr<const Style>& getStyle() const { return _style; }
        
        int getPriority() const { return _priority; }
        void setPriority(int priority) { _priority = priority; }

        float getMinimumGroupDistance() const { return _minimumGroupDistance; }
        void setMinimumGroupDistance(float distance) { _minimumGroupDistance = distance; }

        std::size_t getResidentSize() const {
            return 16 + sizeof(TileLabel);
        }

    private:
        const TileId _tileId;
        const long long _localId;
        const long long _globalId;
        const long long _groupId;
        const std::vector<Font::Glyph> _glyphs;
        const boost::optional<cglib::vec2<float>> _position;
        const std::vector<cglib::vec2<float>> _vertices;
        const std::shared_ptr<const Style> _style;

        int _priority = 0;
        float _minimumGroupDistance = std::numeric_limits<float>::infinity();
    };
} }

#endif
