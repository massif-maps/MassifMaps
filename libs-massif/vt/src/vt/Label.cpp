#include "Label.h"
#include "RenderStats.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace carto::vt {
    Label::Label(const TileLabel& tileLabel, const TileId& tileId, int layerIdx, const cglib::mat4x4<double>& tileMatrix, const std::shared_ptr<const TileTransformer::VertexTransformer>& transformer) :
        _tileId(tileId), _layerIndex(layerIdx), _localId(tileLabel.getLocalId()), _globalId(tileLabel.getGlobalId()), _groupId(tileLabel.getGroupId()), _glyphs(tileLabel.getGlyphs()), _style(tileLabel.getStyle()), _priority(tileLabel.getPlacementInfo().priority), _minimumGroupDistance(tileLabel.getPlacementInfo().minimumGroupDistance), _allowOverlapSameFeatureId(tileLabel.getPlacementInfo().allowOverlapSameFeatureId), _sameFeatureIdDependent(tileLabel.getPlacementInfo().sameFeatureIdDependent), _geoPointIndex(tileLabel.getGeoPointIndex())
    {
        _cachedVertices.reserve(_glyphs.size() * 4);
        _cachedTexCoords.reserve(_glyphs.size() * 4);
        _cachedAttribs.reserve(_glyphs.size() * 4);
        _cachedIndices.reserve(_glyphs.size() * 6);
        
        cglib::vec2<float> pen = cglib::vec2<float>(0, 0);
        _glyphBBox = cglib::bbox2<float>::smallest();
        for (const Font::Glyph& glyph : _glyphs) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                pen = cglib::vec2<float>(0, 0);
            }
            else {
                _glyphBBox.add(pen + glyph.offset);
                _glyphBBox.add(pen + glyph.offset + glyph.size);
            }

            pen += glyph.advance;
        }

        // How far the glyphs reach from the anchor, with the style transform applied the same
        // way findClippedPointPlacement applies it. updatePlacement grows the label's geometry
        // bounds by this before testing them against the frustum, so an anchor that sits just
        // outside the view but whose text reaches into it is not rejected.
        cglib::bbox2<float> glyphBBox = _glyphBBox;
        if (glyphBBox.min(0) <= glyphBBox.max(0)) {
            if (_style->transform) {
                cglib::mat2x2<float> transform = _style->transform->matrix2();
                std::array<cglib::vec2<float>, 4> envelope;
                envelope[0] = cglib::transform(cglib::vec2<float>(glyphBBox.min(0), glyphBBox.min(1)), transform);
                envelope[1] = cglib::transform(cglib::vec2<float>(glyphBBox.min(0), glyphBBox.max(1)), transform);
                envelope[2] = cglib::transform(cglib::vec2<float>(glyphBBox.max(0), glyphBBox.min(1)), transform);
                envelope[3] = cglib::transform(cglib::vec2<float>(glyphBBox.max(0), glyphBBox.max(1)), transform);
                glyphBBox = cglib::bbox2<float>::make_union(envelope.begin(), envelope.end());
            }
            _maxGlyphExtent = std::max(std::max(std::abs(glyphBBox.min(0)), std::abs(glyphBBox.max(0))),
                                       std::max(std::abs(glyphBBox.min(1)), std::abs(glyphBBox.max(1))));
        }

        if (tileLabel.getPosition()) {
            const cglib::vec2<float> pos = *tileLabel.getPosition();
            cglib::vec3<double> position = cglib::transform_point(cglib::vec3<double>::convert(transformer->calculatePoint(pos)), tileMatrix);
            cglib::vec3<float> normal = transformer->calculateNormal(pos);
            cglib::vec3<float> xAxis = transformer->calculateVector(pos, cglib::vec2<float>(1, 0));
            cglib::vec3<float> yAxis = transformer->calculateVector(pos, cglib::vec2<float>(0, -1));
            _tilePoints.emplace_back(_tileId, _localId, position, cglib::unit(normal), cglib::unit(xAxis), cglib::unit(yAxis));
        }

        if (!tileLabel.getVertices().empty()) {
            std::vector<cglib::vec3<double>> vertices;
            vertices.reserve(tileLabel.getVertices().size());
            cglib::vec3<float> normal(0, 0, 0);
            for (const cglib::vec2<float>& pos : tileLabel.getVertices()) {
                cglib::vec3<double> position = cglib::transform_point(cglib::vec3<double>::convert(transformer->calculatePoint(pos)), tileMatrix);
                normal += transformer->calculateNormal(pos);
                vertices.push_back(position);
            }
            _tileLines.emplace_back(_tileId, _localId, std::move(vertices), cglib::unit(normal));
        }
    }

    void Label::mergeGeometries(Label& label) {
        for (TilePoint& tilePoint : label._tilePoints) {
            if (std::find(_tilePoints.begin(), _tilePoints.end(), tilePoint) == _tilePoints.end()) {
                _tilePoints.push_back(std::move(tilePoint));
            }
        }

        for (TileLine& tileLine : label._tileLines) {
            if (std::find(_tileLines.begin(), _tileLines.end(), tileLine) == _tileLines.end()) {
                _tileLines.push_back(std::move(tileLine));
            }
        }

        _geometryBBoxValid = false;
    }
    
    cglib::bbox3<double> Label::calculateGeometryBBox(const ViewState& viewState) const {
        if (!_geometryBBoxValid) {
            _geometryBBox = cglib::bbox3<double>::smallest();
            for (const TilePoint& tilePoint : _tilePoints) {
                _geometryBBox.add(tilePoint.position);
            }
            for (const TileLine& tileLine : _tileLines) {
                for (const cglib::vec3<double>& vertex : tileLine.vertices) {
                    _geometryBBox.add(vertex);
                }
            }
            _geometryBBoxValid = true;
        }

        // Line placements need no margin - findClippedLinePlacement clips the raw vertices -
        // but point placements do, and growing a line label's bounds only makes the rejection
        // more conservative.
        double margin = _maxGlyphExtent * _style->scale * viewState.zoomScale;
        cglib::vec3<double> marginVec(margin, margin, margin);
        return cglib::bbox3<double>(_geometryBBox.min - marginVec, _geometryBBox.max + marginVec);
    }

    std::shared_ptr<const Label::Placement> Label::buildLinePlacement(const TileLine& tileLine, std::size_t index, const cglib::vec3<double>& position) const {
        // Keeps the anchor where it is horizontally and takes its height from the line's
        // own chord, so the anchor sits exactly on the geometry the glyphs are laid out
        // along - the vertex heights and the anchor height can otherwise come from
        // different elevation states (see updateElevation).
        const cglib::vec3<double>& vertex0 = tileLine.vertices[index];
        const cglib::vec3<double>& vertex1 = tileLine.vertices[index + 1];
        double dx = vertex1(0) - vertex0(0);
        double dy = vertex1(1) - vertex0(1);
        double len2 = dx * dx + dy * dy;
        double t = (len2 > 0 ? ((position(0) - vertex0(0)) * dx + (position(1) - vertex0(1)) * dy) / len2 : 0);
        t = std::max(0.0, std::min(1.0, t));
        cglib::vec3<double> pos = vertex0 + (vertex1 - vertex0) * t;
        return std::make_shared<const Placement>(tileLine.tileId, tileLine.localId, tileLine.vertices, index, pos, tileLine.normal);
    }

    void Label::snapPlacement(const Label& label) {
        _placement = label._placement;
        _cachedFlippedPlacement = label._cachedFlippedPlacement;
        if (!_placement) {
            return;
        }
#if CARTO_VT_RENDER_STATS
        VT_STAT_INC(snapPlacements);
        cglib::vec3<double> oldPosition = _placement->position;
#endif

        // Prefer re-snapping onto the same source geometry (tile + feature) the placement
        // was attached to. The merged geometry lists contain one copy of the feature per
        // tile and are rebuilt in tile order whenever the visible tile set changes, so an
        // unbiased nearest-geometry search can flip between copies clipped differently by
        // neighbouring tiles. The rebuilt placement may then fail line fitting or move,
        // which shows up as labels jumping or disappearing while panning.
        const Placement* oldPlacement = label._placement.get();

        _cachedFlippedPlacement.reset();
        if (!_tilePoints.empty()) {
            _placement = findSnappedPointPlacement(_placement->position, _tilePoints, oldPlacement);
            if (_placement && !_tileLines.empty()) {
                _placement = findSnappedLinePlacement(_placement->position, _tileLines, oldPlacement);
            }
        }
        else if (!_tileLines.empty()) {
            _placement = findSnappedLinePlacement(_placement->position, _tileLines, oldPlacement);
        }

#if CARTO_VT_RENDER_STATS
        if (_placement) {
            double dx = _placement->position(0) - oldPosition(0);
            double dy = _placement->position(1) - oldPosition(1);
            if (dx * dx + dy * dy > SNAP_MOVE_EPSILON * SNAP_MOVE_EPSILON) {
                VT_STAT_INC(snapPlacementsMoved);
            }
        }
#endif
    }

    void Label::updateElevation(const std::function<double(const cglib::vec3<double>&)>& heightFunc) {
        // Refresh anchor heights from the elevation data. Label geometry is built when the
        // tile is decoded, possibly before its elevation data has arrived - this re-anchors
        // the labels onto the terrain when the elevation version changes. A line placement
        // is rebuilt from the re-anchored line (below) rather than just shifted, so the
        // glyph run keeps following the terrain profile it is drawn over.
        bool changed = false;
        for (TilePoint& tilePoint : _tilePoints) {
            double height = heightFunc(tilePoint.position);
            if (height != tilePoint.position(2)) {
                tilePoint.position(2) = height;
                changed = true;
            }
        }
        for (TileLine& tileLine : _tileLines) {
            for (cglib::vec3<double>& vertex : tileLine.vertices) {
                double height = heightFunc(vertex);
                if (height != vertex(2)) {
                    vertex(2) = height;
                    changed = true;
                }
            }
        }

        // The elevation version is global: it changes whenever ANY elevation tile is
        // decoded, while the labels affected are only those over that tile. Re-anchoring a
        // label whose heights did not move would drop its cached vertex data (and rebuild
        // the placement) for nothing.
        if (!changed) {
            return;
        }
        _geometryBBoxValid = false;
        VT_STAT_INC(labelElevationReanchors);
        if (!_placement) {
            return;
        }

        cglib::vec3<double> position = _placement->position;
        std::shared_ptr<const Placement> placement;
        if (!_placement->edges.empty()) {
            // Line placement: Placement::edges are stored RELATIVE to the anchor and were
            // built from the vertex heights of the time. Moving the anchor alone leaves the
            // whole glyph run laid out on the old terrain profile, so the label lifts off
            // the line and snaps back the next time the placement is rebuilt - which reads
            // as the text sliding along the road while elevation tiles stream in. Rebuild
            // the edges from the re-anchored line instead.
            for (const TileLine& tileLine : _tileLines) {
                if (!(tileLine.tileId == _placement->tileId && tileLine.localId == _placement->localId)) {
                    continue;
                }
                if (_placement->index + 1 < tileLine.vertices.size()) {
                    placement = buildLinePlacement(tileLine, _placement->index, position);
                }
                break;
            }
        }
        if (!placement) {
            position(2) = heightFunc(position);
            auto pointPlacement = std::make_shared<Placement>(*_placement);
            pointPlacement->position = position;
            placement = std::move(pointPlacement);
        }
        _placement = std::move(placement);
        _cachedFlippedPlacement.reset();
        _cachedPlacement.reset();
        _cachedValid = false;
    }

    bool Label::updatePlacement(const ViewState& viewState) {
        VT_STAT_INC(placementUpdates);
        if (_placement) {
            std::array<cglib::vec3<float>, 4> envelope;
            calculateEnvelope(viewState, envelope);
            cglib::bbox3<double> bbox = cglib::bbox3<double>::smallest();
            for (const cglib::vec3<float>& pos : envelope) {
                bbox.add(viewState.origin + cglib::vec3<double>::convert(pos));
            }
            if (viewState.frustum.inside(bbox)) {
                return false;
            }
        }

        if (_tilePoints.empty() && _tileLines.empty()) {
            return false;
        }

        // Split by what is actually being thrown away: only a re-anchor of a label that was
        // both placed and visible can be seen as the label moving. The rest is a label the
        // user can not see, retrying a placement that keeps failing.
        if (!_placement) {
            VT_STAT_INC(placementReanchorsNull);
        }
        else if (_visible) {
            VT_STAT_INC(placementReanchorsVisible);
        }
        else {
            VT_STAT_INC(placementReanchorsHidden);
        }

        // Nothing of this label's geometry is in view, so the clipped searches below can only
        // walk all of it and return nothing - which is most of the placement work on a
        // typical frame, because the loaded tile set extends well past the viewport. Decide
        // it once against the cached geometry bounds instead.
        //
        // The placement still has to be dropped rather than kept: an invalid label is what
        // excludes it from the culler, and a label that kept a placement while off screen
        // would go on claiming grid cells (screen positions are clamped into the grid, so it
        // would claim border cells) and hide labels that are actually visible.
        if (!viewState.frustum.inside(calculateGeometryBBox(viewState))) {
            _cachedFlippedPlacement.reset();
            if (!_placement) {
                return false; // already unplaced, nothing changed - do not reset the opacity
            }
            _placement.reset();
            return true;
        }
        VT_STAT_INC(placementSearches);

        _cachedFlippedPlacement.reset();
        if (!_tilePoints.empty()) {
            _placement = findClippedPointPlacement(viewState, _tilePoints);
            if (_placement && !_tileLines.empty()) {
                _placement = findSnappedLinePlacement(_placement->position, _tileLines);
            }
            return true;
        }
        _placement = findClippedLinePlacement(viewState, _tileLines);
        return true;
    }

    bool Label::calculateCenter(cglib::vec3<double>& pos) const {
        if (!_placement) {
            return false;
        }

        pos = _placement->position;
        return true;
    }

    float Label::calculateTerrainScaleFactor(const Placement& placement, const ViewState& viewState) const {
        // With planar 3D terrain, labels keep a CONSTANT ON-SCREEN SIZE (tangram-style):
        // the label world size is derived from the zoom level only, so the perspective
        // divide would otherwise scale labels by their distance - drastically oversizing
        // labels lifted onto high mountains and shrinking labels towards the horizon.
        // Rescale by the ratio of the label view depth to the focus depth (where the view
        // axis meets the ground plane), which exactly cancels the perspective scaling.
        if (!viewState.planarTerrain) {
            return 1.0f;
        }
        cglib::vec3<double> viewDir = -cglib::vec3<double>::convert(viewState.orientation[2]);
        double depth = cglib::dot_product(placement.position - viewState.origin, viewDir);
        if (!(depth > 0)) {
            return 1.0f;
        }
        double focusDepth = (viewDir(2) < 0 ? viewState.origin(2) / -viewDir(2) : depth);
        if (!(focusDepth > 0)) {
            return 1.0f;
        }
        float factor = static_cast<float>(depth / focusDepth);
        // Quantize to ~1% steps: line label vertex data is cached by scale and would
        // otherwise be rebuilt on every frame while the camera moves
        factor = std::exp2(std::round(std::log2(factor) * 64.0f) / 64.0f);
        return std::min(8.0f, std::max(0.05f, factor));
    }

    bool Label::calculateEnvelope(float size, float buffer, const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const {
        std::shared_ptr<const Placement> placement = getPlacement(viewState);
        float scale = size * viewState.zoomScale * _style->scale;
        if (placement) {
            scale *= calculateTerrainScaleFactor(*placement, viewState);
        }
        if (!placement || scale <= 0) {
            cglib::vec3<float> origin(0, 0, static_cast<float>(-viewState.origin(2)));
            for (int i = 0; i < 4; i++) {
                envelope[i] = origin;
            }
            return false;
        }

        float padding = buffer * viewState.zoomScale * _style->scale * calculateTerrainScaleFactor(*placement, viewState) / std::sqrt(2.0f);
        cglib::vec3<float> origin, xAxis, yAxis;
        setupCoordinateSystem(viewState, placement, origin, xAxis, yAxis);

        bool valid = cglib::dot_product(viewState.orientation[2], placement->normal) > MIN_BILLBOARD_VIEW_NORMAL_DOTPRODUCT;
        if (_style->orientation == LabelOrientation::LINE) {
            // For line orientation, we have to calculate vertex data and then project vertices to the principal axes
            if (scale != _cachedScale || placement != _cachedPlacement) {
                _cachedVertices.clear();
                _cachedTexCoords.clear();
                _cachedAttribs.clear();
                _cachedIndices.clear();
                _cachedValid = buildLineVertexData(placement, scale, _cachedVertices, _cachedTexCoords, _cachedAttribs, _cachedIndices);
                _cachedScale = scale;
                _cachedPlacement = placement;
            }

            float minX = std::numeric_limits<float>::max(), maxX = -std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max(), maxY = -std::numeric_limits<float>::max();
            for (const cglib::vec3<float>& vertex : _cachedVertices) {
                cglib::vec3<float> pos = origin + vertex;
                float x = cglib::dot_product(xAxis, pos);
                float y = cglib::dot_product(yAxis, pos);
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
            minX -= padding; maxX += padding;
            minY -= padding; maxY += padding;

            cglib::vec3<float> zAxis = cglib::vector_product(xAxis, yAxis);
            cglib::vec3<float> zOrigin = zAxis * cglib::dot_product(origin, zAxis);

            envelope[0] = zOrigin + xAxis * minX + yAxis * minY;
            envelope[1] = zOrigin + xAxis * maxX + yAxis * minY;
            envelope[2] = zOrigin + xAxis * maxX + yAxis * maxY;
            envelope[3] = zOrigin + xAxis * minX + yAxis * maxY;

            valid = valid && _cachedValid;
        }
        else {
            // Use bounding box for envelope
            float minX = _glyphBBox.min(0) * scale - padding, maxX = _glyphBBox.max(0) * scale + padding;
            float minY = _glyphBBox.min(1) * scale - padding, maxY = _glyphBBox.max(1) * scale + padding;
            if (_style->transform) {
                cglib::mat2x2<float> transform = _style->transform->matrix2();
                cglib::vec2<float> p00 = cglib::transform(cglib::vec2<float>(minX, minY), transform);
                cglib::vec2<float> p01 = cglib::transform(cglib::vec2<float>(minX, maxY), transform);
                cglib::vec2<float> p10 = cglib::transform(cglib::vec2<float>(maxX, minY), transform);
                cglib::vec2<float> p11 = cglib::transform(cglib::vec2<float>(maxX, maxY), transform);
                envelope[0] = origin + xAxis * p00(0) + yAxis * p00(1);
                envelope[1] = origin + xAxis * p10(0) + yAxis * p10(1);
                envelope[2] = origin + xAxis * p11(0) + yAxis * p11(1);
                envelope[3] = origin + xAxis * p01(0) + yAxis * p01(1);
            }
            else {
                envelope[0] = origin + xAxis * minX + yAxis * minY;
                envelope[1] = origin + xAxis * maxX + yAxis * minY;
                envelope[2] = origin + xAxis * maxX + yAxis * maxY;
                envelope[3] = origin + xAxis * minX + yAxis * maxY;
            }
        }
        return valid;
    }

    bool Label::calculateVertexData(float size, const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        std::shared_ptr<const Placement> placement = getPlacement(viewState);
        float scale = size * viewState.zoomScale * _style->scale;
        if (placement) {
            scale *= calculateTerrainScaleFactor(*placement, viewState);
        }
        if (!placement || scale <= 0) {
            return false;
        }

        // Build vertex data cache
        bool valid = cglib::dot_product(viewState.orientation[2], placement->normal) > MIN_BILLBOARD_VIEW_NORMAL_DOTPRODUCT;
        if (_style->orientation == LabelOrientation::LINE) {
            // Check if cached vertex data can be used
            if (scale != _cachedScale || placement != _cachedPlacement) {
                _cachedVertices.clear();
                _cachedTexCoords.clear();
                _cachedAttribs.clear();
                _cachedIndices.clear();
                _cachedValid = buildLineVertexData(placement, scale, _cachedVertices, _cachedTexCoords, _cachedAttribs, _cachedIndices);
                _cachedScale = scale;
                _cachedPlacement = placement;
            }
            if (_cachedVertices.size() > MAX_LABEL_VERTICES) {
                return false;
            }

            cglib::vec3<float> origin = cglib::vec3<float>::convert(placement->position - viewState.origin);
            for (const cglib::vec3<float>& vertex : _cachedVertices) {
                vertices.append(origin + vertex);
            }

            valid = valid && _cachedValid;
        }
        else {
            // If no cached data, recalculate and cache it
            if (!_cachedValid) {
                _cachedVertices.clear();
                _cachedTexCoords.clear();
                _cachedAttribs.clear();
                _cachedIndices.clear();
                buildPointVertexData(_cachedVertices, _cachedTexCoords, _cachedAttribs, _cachedIndices);
                _cachedValid = true;
            }
            if (_cachedVertices.size() > MAX_LABEL_VERTICES) {
                return false;
            }

            cglib::vec3<float> origin, xAxis, yAxis;
            setupCoordinateSystem(viewState, placement, origin, xAxis, yAxis);
            for (const cglib::vec3<float>& vertex : _cachedVertices) {
                vertices.append(origin + xAxis * (vertex(0) * scale) + yAxis * (vertex(1) * scale));
            }
        }

        normals.fill(placement->normal, _cachedVertices.size());
        texCoords.copy(_cachedTexCoords, 0, _cachedTexCoords.size());

        if (haloStyleIndex >= 0) {
            for (const cglib::vec4<std::int8_t>& attrib : _cachedAttribs) {
                attribs.append(cglib::vec4<std::int8_t>(static_cast<std::int8_t>(haloStyleIndex), attrib(1), static_cast<std::int8_t>(_opacity * 127.0f), 0));
            }
            
            std::uint16_t offset = static_cast<std::uint16_t>(vertices.size() - _cachedVertices.size());
            for (std::uint16_t idx : _cachedIndices) {
                indices.append(idx + offset);
            }

            vertices.copy(vertices, vertices.size() - _cachedVertices.size(), _cachedVertices.size());

            normals.fill(placement->normal, _cachedVertices.size());
            texCoords.copy(_cachedTexCoords, 0, _cachedTexCoords.size());
        }

        for (const cglib::vec4<std::int8_t>& attrib : _cachedAttribs) {
            attribs.append(cglib::vec4<std::int8_t>(static_cast<std::int8_t>(styleIndex), attrib(1), static_cast<std::int8_t>(_opacity * 127.0f), 0));
        }
        
        std::uint16_t offset = static_cast<std::uint16_t>(vertices.size() - _cachedVertices.size());
        for (std::uint16_t idx : _cachedIndices) {
            if (!(haloStyleIndex >= 0 && _cachedAttribs[idx](1) != 0)) { // do not add non-SDF glyphs if halo is enabled in the second pass
                indices.append(idx + offset);
            }
        }

        return valid;
    }

    void Label::buildPointVertexData(VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        cglib::vec2<float> pen(0, 0);
        for (const Font::Glyph& glyph : _glyphs) {
            // If carriage return, reposition pen and state to the initial position
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                pen = cglib::vec2<float>(0, 0);
            }
            else if (glyph.codePoint != Font::SPACE_CODEPOINT) {
                std::uint16_t i0 = static_cast<std::uint16_t>(vertices.size());
                indices.append(i0 + 0, i0 + 1, i0 + 2);
                indices.append(i0 + 0, i0 + 2, i0 + 3);

                std::int16_t u0 = static_cast<std::int16_t>(glyph.baseGlyph.x), u1 = static_cast<std::int16_t>(glyph.baseGlyph.x + glyph.baseGlyph.width);
                std::int16_t v0 = static_cast<std::int16_t>(glyph.baseGlyph.y), v1 = static_cast<std::int16_t>(glyph.baseGlyph.y + glyph.baseGlyph.height);
                texCoords.append(cglib::vec2<std::int16_t>(u0, v1), cglib::vec2<std::int16_t>(u1, v1), cglib::vec2<std::int16_t>(u1, v0), cglib::vec2<std::int16_t>(u0, v0));

                cglib::vec4<std::int8_t> attrib(0, static_cast<std::int8_t>(glyph.baseGlyph.mode), 0, 0);
                attribs.append(attrib, attrib, attrib, attrib);

                if (_style->transform) {
                    cglib::mat2x2<float> transform = _style->transform->matrix2();
                    cglib::vec2<float> p0 = cglib::transform(pen + glyph.offset, transform);
                    cglib::vec2<float> p1 = cglib::transform(pen + glyph.offset + cglib::vec2<float>(glyph.size(0), 0), transform);
                    cglib::vec2<float> p2 = cglib::transform(pen + glyph.offset + glyph.size, transform);
                    cglib::vec2<float> p3 = cglib::transform(pen + glyph.offset + cglib::vec2<float>(0, glyph.size(1)), transform);
                    vertices.append(cglib::vec3<float>(p0(0), p0(1), 0), cglib::vec3<float>(p1(0), p1(1), 0), cglib::vec3<float>(p2(0), p2(1), 0), cglib::vec3<float>(p3(0), p3(1), 0));
                }
                else {
                    cglib::vec2<float> p0 = pen + glyph.offset;
                    cglib::vec2<float> p3 = pen + glyph.offset + glyph.size;
                    vertices.append(cglib::vec3<float>(p0(0), p0(1), 0), cglib::vec3<float>(p3(0), p0(1), 0), cglib::vec3<float>(p3(0), p3(1), 0), cglib::vec3<float>(p0(0), p3(1), 0));
                }
            }

            // Move pen
            pen += glyph.advance;
        }
    }

    bool Label::buildLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        const std::vector<Placement::Edge>& edges = placement->edges;
        if (edges.empty()) {
            return false;
        }
        std::size_t edgeIndex = placement->index;
        cglib::vec2<float> pen(cglib::dot_product(-edges[edgeIndex].position0, edges[edgeIndex].xAxis), 0);

        bool valid = true;
        for (std::size_t i = 0; i < _glyphs.size(); i++) {
            const Font::Glyph& glyph = _glyphs[i];

            cglib::vec3<float> xAxis = edges[edgeIndex].xAxis;
            cglib::vec3<float> yAxis = edges[edgeIndex].yAxis;
            cglib::vec3<float> origin = edges[edgeIndex].position0 + xAxis * pen(0) + yAxis * pen(1);

            // If carriage return, reposition pen and state to the initial position
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                edgeIndex = placement->index;
                pen = cglib::vec2<float>(cglib::dot_product(-edges[edgeIndex].position0, edges[edgeIndex].xAxis), 0);
            }

            // Move pen
            pen += glyph.advance * scale;

            // Check if we the pen has gone 'over' line segment
            if (glyph.advance(0) > 0) {
                cglib::vec3<float> yAxisBase = yAxis;
                cglib::vec3<float> originBase = origin;
                while (true) {
                    float edgeLen = cglib::length(edges[edgeIndex].position1 - edges[edgeIndex].position0);
                    float offset1 = cglib::dot_product(edges[edgeIndex].binormal1 * pen(1), edges[edgeIndex].xAxis);
                    if (pen(0) < edgeLen + offset1) {
                        break;
                    }
                    if (edgeIndex + 1 >= edges.size()) {
                        valid = false;
                        break;
                    }
                    edgeIndex++;

                    cglib::vec3<float> edgePos0 = edges[edgeIndex].position0 + edges[edgeIndex].binormal0 * pen(1);
                    cglib::vec3<float> edgePos1 = edges[edgeIndex].position1 + edges[edgeIndex].binormal1 * pen(1);
                    cglib::vec3<float> target = origin;

                    // Do complex multi-iteration fitting
                    for (unsigned int iter = 0; true; iter++) {
                        cglib::vec3<float> dq = edgePos0 - origin;
                        cglib::vec3<float> dp = edgePos1 - edgePos0;
                        float a = cglib::dot_product(dp, dp);
                        float b = cglib::dot_product(dp, dq);
                        float c = cglib::dot_product(dq, dq) - (glyph.advance(0) * scale) * (glyph.advance(0) * scale);
                        float d = b * b - a * c;
                        if (d < 0) {
                            d = 0;
                            valid = false;
                        }
                        float t1 = (-b + std::sqrt(d)) / a;
                        target = edgePos0 + dp * t1;
                        xAxis = cglib::unit(target - origin);
                        yAxis = cglib::unit(cglib::vector_product(placement->normal, xAxis));

                        if (iter >= MAX_LINE_FITTING_ITERATIONS) {
                            break;
                        }

                        float delta = 0;
                        if (i > 0 && _glyphs[i - 1].codePoint != Font::CR_CODEPOINT) {
                            float sin = cglib::dot_product(xAxis, yAxisBase);
                            delta = sin * (sin < 0 ? -_style->descent * 0.5f : _style->ascent * 0.5f) * scale;
                        }
                        origin = originBase + xAxis * delta;
                    }

                    float delta = 0;
                    if (i + 1 < _glyphs.size() && _glyphs[i + 1].codePoint != Font::CR_CODEPOINT) {
                        float sin = -cglib::dot_product(xAxis, edges[edgeIndex].yAxis);
                        delta = sin * (sin < 0 ? -_style->descent * 0.5f : _style->ascent * 0.5f) * scale;
                    }
                    pen(0) = cglib::dot_product(edges[edgeIndex].xAxis, target - edges[edgeIndex].position0) + delta;
                }

                if (cglib::dot_product(xAxis, placement->xAxis) < MIN_LINE_SEGMENT_DOTPRODUCT) {
                    valid = false;
                }
            }

            // Render glyph
            if (glyph.codePoint != Font::SPACE_CODEPOINT && glyph.codePoint != Font::CR_CODEPOINT) {
                std::uint16_t i0 = static_cast<std::uint16_t>(vertices.size());
                indices.append(i0 + 0, i0 + 1, i0 + 2);
                indices.append(i0 + 0, i0 + 2, i0 + 3);

                std::int16_t u0 = static_cast<std::int16_t>(glyph.baseGlyph.x), u1 = static_cast<std::int16_t>(glyph.baseGlyph.x + glyph.baseGlyph.width);
                std::int16_t v0 = static_cast<std::int16_t>(glyph.baseGlyph.y), v1 = static_cast<std::int16_t>(glyph.baseGlyph.y + glyph.baseGlyph.height);
                texCoords.append(cglib::vec2<std::int16_t>(u0, v1), cglib::vec2<std::int16_t>(u1, v1), cglib::vec2<std::int16_t>(u1, v0), cglib::vec2<std::int16_t>(u0, v0));

                cglib::vec4<std::int8_t> attrib(0, static_cast<std::int8_t>(glyph.baseGlyph.mode), 0, 0);
                attribs.append(attrib, attrib, attrib, attrib);

                cglib::vec2<float> p0 = glyph.offset * scale;
                cglib::vec2<float> p3 = (glyph.offset + glyph.size) * scale;
                vertices.append(origin + xAxis * p0(0) + yAxis * p0(1), origin + xAxis * p3(0) + yAxis * p0(1), origin + xAxis * p3(0) + yAxis * p3(1), origin + xAxis * p0(0) + yAxis * p3(1));
            }

            // Handle backwards moving
            if (glyph.advance(0) < 0) {
                while (true) {
                    float offset0 = cglib::dot_product(edges[edgeIndex].binormal0 * pen(1), edges[edgeIndex].xAxis);
                    if (pen(0) >= offset0) {
                        break;
                    }
                    if (edgeIndex == 0) {
                        valid = false;
                        break;
                    }
                    edgeIndex--;

                    float offset1 = cglib::dot_product(edges[edgeIndex].binormal1 * pen(1), edges[edgeIndex].xAxis);
                    pen(0) += cglib::length(edges[edgeIndex].position1 - edges[edgeIndex].position0) + offset1 - offset0;
                }
            }
        }

        return valid;
    }

    void Label::setupCoordinateSystem(const ViewState& viewState, const std::shared_ptr<const Placement>& placement, cglib::vec3<float>& origin, cglib::vec3<float>& xAxis, cglib::vec3<float>& yAxis) const {
        cglib::vec3<double> position = placement->position;
        if (viewState.planarTerrain && _style->orientation != LabelOrientation::LINE && viewState.resolution > 0) {
            // Snap the label anchor to a quarter of the (normalized) pixel grid: glyphs then
            // rasterize at a stable subpixel phase, which keeps text noticeably sharper and
            // shimmer-free (tangram-style screen-space anchoring)
            cglib::mat4x4<double> viewProjMatrix = viewState.projectionMatrix * viewState.cameraMatrix;
            cglib::vec4<double> clipPos = cglib::transform(cglib::vec4<double>(position(0), position(1), position(2), 1), viewProjMatrix);
            if (clipPos(3) > 0) {
                double screenWidth = viewState.resolution * viewState.aspect;
                double screenHeight = viewState.resolution;
                double pixelX = (clipPos(0) / clipPos(3) * 0.5 + 0.5) * screenWidth;
                double pixelY = (clipPos(1) / clipPos(3) * 0.5 + 0.5) * screenHeight;
                double snappedX = std::round(pixelX * 4.0) * 0.25;
                double snappedY = std::round(pixelY * 4.0) * 0.25;
                cglib::vec3<double> snappedNDC((snappedX / screenWidth - 0.5) * 2.0, (snappedY / screenHeight - 0.5) * 2.0, clipPos(2) / clipPos(3));
                position = cglib::transform_point(snappedNDC, cglib::inverse(viewProjMatrix));
            }
        }
        origin = cglib::vec3<float>::convert(position - viewState.origin);
        switch (_style->orientation) {
        case LabelOrientation::BILLBOARD_2D:
            xAxis = cglib::unit(cglib::vector_product(viewState.orientation[1], placement->normal));
            yAxis = cglib::unit(cglib::vector_product(placement->normal, xAxis));
            break;
        case LabelOrientation::LINE_BILLBOARD_3D:
        case LabelOrientation::BILLBOARD_3D:
            xAxis = viewState.orientation[0];
            yAxis = viewState.orientation[1];
            break;
        default: // LabelOrientation::POINT, LabelOrientation::LINE
            xAxis = placement->xAxis;
            yAxis = placement->yAxis;
            break;
        }
    }

    std::shared_ptr<const Label::Placement> Label::getPlacement(const ViewState& viewState) const {
        if (!_placement) {
            return std::shared_ptr<const Placement>();
        }

        if (!_style->autoflip) {
            return _placement;
        }

        cglib::vec3<float> xAxis = _placement->xAxis;
        if (_style->transform) {
            cglib::mat2x2<float> transform = _style->transform->matrix2();
            cglib::vec2<float> p10 = cglib::transform(cglib::vec2<float>(1, 0), transform);
            xAxis = _placement->xAxis * p10(0) + _placement->yAxis * p10(1);
        }
        if (cglib::dot_product(xAxis, viewState.orientation[0]) > 0) {
            return _placement;
        }

        if (!_cachedFlippedPlacement) {
            Placement flippedPlacement(*_placement);
            flippedPlacement.reverse();
            _cachedFlippedPlacement = std::make_shared<Placement>(std::move(flippedPlacement));
        }
        return _cachedFlippedPlacement;
    }

    std::shared_ptr<const Label::Placement> Label::findSnappedPointPlacement(const cglib::vec3<double>& position, const std::list<TilePoint>& tilePoints, const Placement* oldPlacement) const {
        const TilePoint* bestTilePoint = nullptr;
        double bestDist = std::numeric_limits<double>::infinity();
        bool bestSameSource = false;
        for (const TilePoint& tilePoint : tilePoints) {
            // A candidate from the placement's original source geometry always wins over
            // copies of the feature coming from other tiles (see snapPlacement).
            bool sameSource = oldPlacement && tilePoint.tileId == oldPlacement->tileId && tilePoint.localId == oldPlacement->localId;
            if (bestSameSource && !sameSource) {
                continue;
            }
            double dist = cglib::length(tilePoint.position - position);
            if (dist < bestDist || (sameSource && !bestSameSource)) {
                bestTilePoint = &tilePoint;
                bestDist = dist;
                bestSameSource = sameSource;
            }
        }
        if (!bestTilePoint) {
            return std::shared_ptr<const Placement>();
        }

        return std::make_shared<const Placement>(bestTilePoint->tileId, bestTilePoint->localId, bestTilePoint->position, bestTilePoint->normal, bestTilePoint->xAxis, bestTilePoint->yAxis);
    }

    std::shared_ptr<const Label::Placement> Label::findSnappedLinePlacement(const cglib::vec3<double>& position, const std::list<TileLine>& tileLines, const Placement* oldPlacement) const {
        // Exact preservation: when the placement's own source line is still there, keep the
        // anchor on the segment it already sits on instead of re-deriving it. Re-deriving
        // scores candidates by distance, and the anchor only scores an exact 0 while it lies
        // exactly on the line. On terrain it does not: a label rebuilt from a freshly decoded
        // tile carries the vertex heights of its decode time while the anchor carries the
        // current ones, so every segment scores nonzero, the mid-line weight below decides
        // instead, and the anchor creeps toward the middle of the road - on every tile-set
        // change, which is several times a second while tiles stream in.
        if (oldPlacement) {
            for (const TileLine& tileLine : tileLines) {
                if (!(tileLine.tileId == oldPlacement->tileId && tileLine.localId == oldPlacement->localId)) {
                    continue;
                }
                if (oldPlacement->index + 1 < tileLine.vertices.size()) {
                    return buildLinePlacement(tileLine, oldPlacement->index, position);
                }
                break;
            }
        }

        const TileLine* bestTileLine = nullptr;
        std::size_t bestIndex = 0;
        cglib::vec3<double> bestPos = position;
        double bestDist = std::numeric_limits<double>::infinity();
        bool bestSameSource = false;
        for (const TileLine& tileLine : tileLines) {
            // A candidate from the placement's original source geometry always wins over
            // copies of the feature coming from other tiles (see snapPlacement).
            bool sameSource = oldPlacement && tileLine.tileId == oldPlacement->tileId && tileLine.localId == oldPlacement->localId;
            if (bestSameSource && !sameSource) {
                continue;
            }
            if (sameSource && !bestSameSource) {
                bestTileLine = nullptr;
                bestIndex = 0;
                bestPos = position;
                bestDist = std::numeric_limits<double>::infinity();
                bestSameSource = true;
            }
            // Try to find a closest point on vertices to the given position. Distances are
            // measured horizontally: the vertex heights and the anchor height can come from
            // different elevation states, and a height mismatch must not decide which
            // segment of the road the label ends up on.
            for (std::size_t j = 1; j < tileLine.vertices.size(); j++) {
                cglib::vec3<double> edgeVec = tileLine.vertices[j] - tileLine.vertices[j - 1];
                double edgeLen2 = edgeVec(0) * edgeVec(0) + edgeVec(1) * edgeVec(1);
                if (edgeLen2 == 0) {
                    continue;
                }
                cglib::vec3<double> posVec = position - tileLine.vertices[j - 1];
                double t = (edgeVec(0) * posVec(0) + edgeVec(1) * posVec(1)) / edgeLen2;
                cglib::vec3<double> edgePos = tileLine.vertices[j - 1] + edgeVec * std::max(0.0, std::min(1.0, t));
                // The mid-line bias picks a placement far from the line's endpoints, which is
                // what you want when choosing a FRESH anchor. When re-snapping an existing
                // one it does the opposite: a zoom step changes every tile id, so the
                // placement's own source line is gone and this fallback runs - and the bias
                // then drags the anchor away from where it was, toward the middle of the
                // road. Re-snapping takes the nearest point instead.
                double weight = (oldPlacement ? 1.0 : (1.0 / j) + (1.0 / (tileLine.vertices.size() - j)));
                cglib::vec3<double> distVec = edgePos - position;
                double dist = std::sqrt(distVec(0) * distVec(0) + distVec(1) * distVec(1)) * weight;
                if (dist < bestDist) {
                    bestIndex = j - 1;
                    bestTileLine = &tileLine;
                    bestPos = edgePos;
                    bestDist = dist;
                }
            }
        }
        if (!bestTileLine) {
            return std::shared_ptr<const Placement>();
        }

        return std::make_shared<const Placement>(bestTileLine->tileId, bestTileLine->localId, bestTileLine->vertices, bestIndex, bestPos, bestTileLine->normal);
    }

    std::shared_ptr<const Label::Placement> Label::findClippedPointPlacement(const ViewState& viewState, const std::list<TilePoint>& tilePoints) const {
        cglib::bbox2<float> bbox = _glyphBBox;
        if (_style->transform) {
            cglib::mat2x2<float> transform = _style->transform->matrix2();
            std::array<cglib::vec2<float>, 4> envelope;
            envelope[0] = cglib::transform(cglib::vec2<float>(bbox.min(0), bbox.min(1)), transform);
            envelope[1] = cglib::transform(cglib::vec2<float>(bbox.min(0), bbox.max(1)), transform);
            envelope[2] = cglib::transform(cglib::vec2<float>(bbox.max(0), bbox.min(1)), transform);
            envelope[3] = cglib::transform(cglib::vec2<float>(bbox.max(0), bbox.max(1)), transform);
            bbox = cglib::bbox2<float>::make_union(envelope.begin(), envelope.end());
        }
        
        for (const TilePoint& tilePoint : tilePoints) {
            // Check that text is visible, calculate text distance from all frustum planes
            bool inside = true;
            for (int plane = 0; plane < 6; plane++) {
                float size = 0;
                switch (plane) {
                case 2:
                    size = -bbox.min(1);
                    break;
                case 3:
                    size = bbox.max(1);
                    break;
                case 4:
                    size = bbox.max(0) / viewState.aspect;
                    break;
                case 5:
                    size = -bbox.min(0) / viewState.aspect;
                    break;
                }
                double dist = viewState.frustum.plane_distance(plane, tilePoint.position);
                if (dist < -size * _style->scale * viewState.zoomScale) {
                    inside = false;
                    break;
                }
            }
            if (inside) {
                return std::make_shared<const Placement>(tilePoint.tileId, tilePoint.localId, tilePoint.position, tilePoint.normal, tilePoint.xAxis, tilePoint.yAxis);
            }
        }
        return std::shared_ptr<const Placement>();
    }

    std::shared_ptr<const Label::Placement> Label::findClippedLinePlacement(const ViewState& viewState, const std::list<TileLine>& tileLines) const {
        // Clip each vertex list against frustum, if resulting list is inside frustum, return its center
        double bestLen = (_style->orientation == LabelOrientation::LINE ? (_glyphBBox.size()(0) + EXTRA_PLACEMENT_PIXELS) * _style->scale * viewState.zoomScale : 0);
        std::shared_ptr<const Placement> bestPlacement;
        auto updateBestPlacement = [&](const TileLine& tileLine, std::size_t i0, std::size_t i1) {
            if (i1 < i0 + 2) {
                return;
            }

            std::pair<std::size_t, double> t0(i0, 0);
            std::pair<std::size_t, double> t1(i1 - 2, 1);
            for (int plane = 0; plane < 6; plane++) {
                if (t0 > t1) {
                    break;
                }

                std::pair<std::size_t, double> t0t = t1;
                std::pair<std::size_t, double> t1t = t0;
                double prevDist = viewState.frustum.plane_distance(plane, tileLine.vertices[t0.first]);
                for (std::size_t i = t0.first; i <= t1.first; i++) {
                    double nextDist = viewState.frustum.plane_distance(plane, tileLine.vertices[i + 1]);
                    if (nextDist > 0) {
                        if (prevDist < 0) {
                            t0t = std::min(t0t, std::pair<std::size_t, double>(i, 1 - nextDist / (nextDist - prevDist)));
                        }
                        t1t = std::max(t1t, std::pair<std::size_t, double>(i + 1, 0));
                    }
                    if (prevDist > 0) {
                        if (nextDist < 0) {
                            t1t = std::max(t1t, std::pair<std::size_t, double>(i, 1 - nextDist / (nextDist - prevDist)));
                        }
                        t0t = std::min(t0t, std::pair<std::size_t, double>(i, 0));
                    }
                    prevDist = nextDist;
                }
                t0 = std::max(t0, t0t);
                t1 = std::min(t1, t1t);
            }
            if (t0 < t1) {
                double len = 0;
                for (std::size_t i = t0.first; i <= t1.first; i++) {
                    cglib::vec3<double> pos0 = tileLine.vertices[i];
                    if (i == t0.first) {
                        pos0 = tileLine.vertices[i] * (1 - t0.second) + tileLine.vertices[i + 1] * t0.second;
                    }
                    cglib::vec3<double> pos1 = tileLine.vertices[i + 1];
                    if (i == t1.first) {
                        pos1 = tileLine.vertices[i] * (1 - t1.second) + tileLine.vertices[i + 1] * t1.second;
                    }
                    double diff = cglib::length(pos1 - pos0);
                    len += diff;
                }

                if (len > bestLen) {
                    double ofs = len * 0.5;
                    for (std::size_t i = t0.first; i <= t1.first; i++) {
                        cglib::vec3<double> pos0 = tileLine.vertices[i];
                        if (i == t0.first) {
                            pos0 = tileLine.vertices[i] * (1 - t0.second) + tileLine.vertices[i + 1] * t0.second;
                        }
                        cglib::vec3<double> pos1 = tileLine.vertices[i + 1];
                        if (i == t1.first) {
                            pos1 = tileLine.vertices[i] * (1 - t1.second) + tileLine.vertices[i + 1] * t1.second;
                        }
                        double diff = cglib::length(pos1 - pos0);
                        if (ofs < diff) {
                            cglib::vec3<double> pos = pos0 + (pos1 - pos0) * (ofs / diff); // this assumes central anchor point
                            bestPlacement = std::make_shared<const Placement>(tileLine.tileId, tileLine.localId, tileLine.vertices, i, pos, tileLine.normal);
                            bestLen = len;
                            break;
                        }
                        ofs -= diff;
                    }
                }
            }
        };
        
        // Split vertices list into relatively straight segments
        for (const TileLine& tileLine : tileLines) {
            cglib::bbox3<double> bbox = cglib::bbox3<double>::make_union(tileLine.vertices.begin(), tileLine.vertices.end());
            if (!viewState.frustum.inside(bbox)) {
                continue;
            }

            std::size_t i0 = 0;
            float summedAngle = 0;
            cglib::vec3<double> lastEdgeVec(0, 0, 0);
            for (std::size_t i = 1; i < tileLine.vertices.size(); i++) {
                cglib::vec3<double> edgeVec = cglib::unit(tileLine.vertices[i] - tileLine.vertices[i - 1]);
                if (lastEdgeVec != cglib::vec3<double>::zero()) {
                    float cos = static_cast<float>(cglib::dot_product(edgeVec, lastEdgeVec));
                    float angle = std::acos(std::min(1.0f, std::max(-1.0f, cos)));
                    summedAngle += angle;
                    if (angle > SINGLE_ANGLE_SPLIT_THRESHOLD || summedAngle > SUMMED_ANGLE_SPLIT_THRESHOLD) {
                        updateBestPlacement(tileLine, i0, i);
                        i0 = i - 1;
                        summedAngle = 0;
                    }
                }
                lastEdgeVec = edgeVec;
            }
            updateBestPlacement(tileLine, i0, tileLine.vertices.size());
        }
        return bestPlacement;
    }
}
