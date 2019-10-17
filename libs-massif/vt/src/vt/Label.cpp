#include "Label.h"

#include <algorithm>
#include <map>

namespace carto { namespace vt {
    Label::Label(const TileLabel& tileLabel, const cglib::mat4x4<double>& tileMatrix, const std::shared_ptr<const TileTransformer::VertexTransformer>& transformer) :
        _tileId(tileLabel.getTileId()), _localId(tileLabel.getLocalId()), _globalId(tileLabel.getGlobalId()), _groupId(tileLabel.getGroupId()), _glyphs(tileLabel.getGlyphs()), _style(tileLabel.getStyle()), _priority(tileLabel.getPriority()), _minimumGroupDistance(tileLabel.getMinimumGroupDistance())
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

        if (auto pos = boost::get<cglib::vec2<float>>(&tileLabel.getPosition())) {
            cglib::vec3<double> position = cglib::transform_point(cglib::vec3<double>::convert(transformer->calculatePoint(*pos)), tileMatrix);
            cglib::vec3<float> normal = transformer->calculateNormal(*pos);
            cglib::vec3<float> xAxis = transformer->calculateVector(*pos, cglib::vec2<float>(1, 0));
            cglib::vec3<float> yAxis = transformer->calculateVector(*pos, cglib::vec2<float>(0, -1));
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
    }
    
    void Label::snapPlacement(const Label& label) {
        _placement = label._placement;
        _cachedFlippedPlacement = label._cachedFlippedPlacement;
        if (!_placement) {
            return;
        }

        _cachedFlippedPlacement.reset();
        if (!_tilePoints.empty()) {
            _placement = findSnappedPointPlacement(_placement->position, _tilePoints);
            if (_placement && !_tileLines.empty()) {
                _placement = findSnappedLinePlacement(_placement->position, _tileLines);
            }
            return;
        }
        if (!_tileLines.empty()) {
            _placement = findSnappedLinePlacement(_placement->position, _tileLines);
        }
    }

    bool Label::updatePlacement(const ViewState& viewState) {
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

        _cachedFlippedPlacement.reset();
        if (!_tilePoints.empty()) {
            _placement = findClippedPointPlacement(viewState, _tilePoints);
            if (_placement && !_tileLines.empty()) {
                _placement = findSnappedLinePlacement(_placement->position, _tileLines);
            }
            return true;
        }
        if (!_tileLines.empty()) {
            _placement = findClippedLinePlacement(viewState, _tileLines);
            return true;
        }

        return false;
    }

    bool Label::calculateCenter(cglib::vec3<double>& pos) const {
        if (!_placement) {
            return false;
        }

        pos = _placement->position;
        return true;
    }

    bool Label::calculateEnvelope(float size, float buffer, const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const {
        std::shared_ptr<const Placement> placement = getPlacement(viewState);
        float scale = size * viewState.zoomScale * _style->scale;
        if (!placement || scale <= 0) {
            cglib::vec3<float> origin(0, 0, static_cast<float>(-viewState.origin(2)));
            for (int i = 0; i < 4; i++) {
                envelope[i] = origin;
            }
            return false;
        }

        float padding = buffer * viewState.zoomScale * _style->scale;
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
                cglib::vec2<float> p00 = cglib::transform(cglib::vec2<float>(minX, minY), _style->transform.get());
                cglib::vec2<float> p01 = cglib::transform(cglib::vec2<float>(minX, maxY), _style->transform.get());
                cglib::vec2<float> p10 = cglib::transform(cglib::vec2<float>(maxX, minY), _style->transform.get());
                cglib::vec2<float> p11 = cglib::transform(cglib::vec2<float>(maxX, maxY), _style->transform.get());
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
        attribs.copy(_cachedAttribs, 0, _cachedAttribs.size());
        indices.copy(_cachedIndices, 0, _cachedIndices.size());

        if (haloStyleIndex >= 0) {
            for (cglib::vec4<std::int8_t>* it = attribs.end() - _cachedAttribs.size(); it != attribs.end(); it++) {
                *it = cglib::vec4<std::int8_t>(static_cast<std::int8_t>(haloStyleIndex), std::min((std::int8_t)0, (*it)(1)), static_cast<std::int8_t>(_opacity * 127.0f), 0);
            }
            for (std::uint16_t* it = indices.end() - _cachedIndices.size(); it != indices.end(); it++) {
                *it += static_cast<std::uint16_t>(vertices.size() - _cachedVertices.size());
            }

            vertices.copy(vertices, vertices.size() - _cachedVertices.size(), _cachedVertices.size());
            normals.fill(placement->normal, _cachedVertices.size());
            texCoords.copy(_cachedTexCoords, 0, _cachedTexCoords.size());
            attribs.copy(_cachedAttribs, 0, _cachedAttribs.size());
            indices.copy(_cachedIndices, 0, _cachedIndices.size());
        }

        for (cglib::vec4<std::int8_t>* it = attribs.end() - _cachedAttribs.size(); it != attribs.end(); it++) {
            *it = cglib::vec4<std::int8_t>(static_cast<int>(styleIndex), (*it)(1), static_cast<std::int8_t>(_opacity * 127.0f), 0);
        }
        for (std::uint16_t* it = indices.end() - _cachedIndices.size(); it != indices.end(); it++) {
            *it += static_cast<std::uint16_t>(vertices.size() - _cachedVertices.size());
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

                cglib::vec4<std::int8_t> attrib(0, glyph.baseGlyph.sdfMode ? -1 : 1, 0, 0);
                attribs.append(attrib, attrib, attrib, attrib);

                if (_style->transform) {
                    cglib::vec2<float> p0 = cglib::transform(pen + glyph.offset, _style->transform.get());
                    cglib::vec2<float> p1 = cglib::transform(pen + glyph.offset + cglib::vec2<float>(glyph.size(0), 0), _style->transform.get());
                    cglib::vec2<float> p2 = cglib::transform(pen + glyph.offset + glyph.size, _style->transform.get());
                    cglib::vec2<float> p3 = cglib::transform(pen + glyph.offset + cglib::vec2<float>(0, glyph.size(1)), _style->transform.get());
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
                cglib::vec3<float> xAxisBase = xAxis;
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

                if (cglib::dot_product(xAxis, placement->xAxis) < MIN_SINGLE_SEGMENT_DOTPRODUCT) {
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

                cglib::vec4<std::int8_t> attrib(0, glyph.baseGlyph.sdfMode ? -1 : 1, 0, 0);
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
        origin = cglib::vec3<float>::convert(placement->position - viewState.origin);
        switch (_style->orientation) {
        case LabelOrientation::BILLBOARD_2D:
            xAxis = cglib::unit(cglib::vector_product(viewState.orientation[1], placement->normal));
            yAxis = cglib::unit(cglib::vector_product(placement->normal, xAxis));
            break;
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

        if (!_style->autoflip || cglib::dot_product(_placement->xAxis, viewState.orientation[0]) > 0) {
            return _placement;
        }

        if (!_cachedFlippedPlacement) {
            Placement flippedPlacement(*_placement);
            flippedPlacement.reverse();
            _cachedFlippedPlacement = std::make_shared<Placement>(std::move(flippedPlacement));
        }
        return _cachedFlippedPlacement;
    }

    std::shared_ptr<const Label::Placement> Label::findSnappedPointPlacement(const cglib::vec3<double>& position, const std::list<TilePoint>& tilePoints) const {
        const TilePoint* bestTilePoint = nullptr;
        double bestDist = std::numeric_limits<double>::infinity();
        for (const TilePoint& tilePoint : tilePoints) {
            double dist = cglib::length(tilePoint.position - position);
            if (dist < bestDist) {
                bestTilePoint = &tilePoint;
                bestDist = dist;
            }
        }
        if (!bestTilePoint) {
            return std::shared_ptr<const Placement>();
        }

        return std::make_shared<const Placement>(bestTilePoint->tileId, bestTilePoint->localId, bestTilePoint->position, bestTilePoint->normal, bestTilePoint->xAxis, bestTilePoint->yAxis);
    }

    std::shared_ptr<const Label::Placement> Label::findSnappedLinePlacement(const cglib::vec3<double>& position, const std::list<TileLine>& tileLines) const {
        const TileLine* bestTileLine = nullptr;
        std::size_t bestIndex = 0;
        cglib::vec3<double> bestPos = position;
        double bestDist = std::numeric_limits<double>::infinity();
        for (const TileLine& tileLine : tileLines) {
            // Try to find a closest point on vertices to the given position
            for (std::size_t j = 1; j < tileLine.vertices.size(); j++) {
                cglib::vec3<double> edgeVec = tileLine.vertices[j] - tileLine.vertices[j - 1];
                double edgeLen2 = cglib::dot_product(edgeVec, edgeVec);
                if (edgeLen2 == 0) {
                    continue;
                }
                double t = cglib::dot_product(edgeVec, position - tileLine.vertices[j - 1]) / edgeLen2;
                cglib::vec3<double> edgePos = tileLine.vertices[j - 1] + edgeVec * std::max(0.0, std::min(1.0, t));
                double weight = (1.0 / j) + (1.0 / (tileLine.vertices.size() - j)); // favor positions far from endpoint, will result in more stable placements
                double dist = cglib::length(edgePos - position) * weight;
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
            std::array<cglib::vec2<float>, 4> envelope;
            envelope[0] = cglib::transform(cglib::vec2<float>(bbox.min(0), bbox.min(1)), _style->transform.get());
            envelope[1] = cglib::transform(cglib::vec2<float>(bbox.min(0), bbox.max(1)), _style->transform.get());
            envelope[2] = cglib::transform(cglib::vec2<float>(bbox.max(0), bbox.min(1)), _style->transform.get());
            envelope[3] = cglib::transform(cglib::vec2<float>(bbox.max(0), bbox.max(1)), _style->transform.get());
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
            std::size_t i0 = 0;
            float summedAngle = 0;
            cglib::vec3<double> lastEdgeVec(0, 0, 0);
            for (std::size_t i = 1; i < tileLine.vertices.size(); i++) {
                cglib::vec3<double> edgeVec = cglib::unit(tileLine.vertices[i] - tileLine.vertices[i - 1]);
                if (lastEdgeVec != cglib::vec3<double>::zero()) {
                    float cos = static_cast<float>(cglib::dot_product(edgeVec, lastEdgeVec));
                    float angle = std::acos(std::min(1.0f, std::max(-1.0f, cos)));
                    summedAngle += angle;
                    if (cos < MIN_SINGLE_SEGMENT_DOTPRODUCT || summedAngle > MAX_SUMMED_SEGMENT_ANGLE) {
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
} }
