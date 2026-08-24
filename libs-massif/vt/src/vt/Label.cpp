#include "Label.h"
#include "RenderStats.h"

#include <algorithm>
#include <cmath>
#include <map>



namespace massif::vt {
    Label::Label(const TileLabel& tileLabel, const TileId& tileId, int layerIdx, const cglib::mat4x4<double>& tileMatrix, const std::shared_ptr<const TileTransformer::VertexTransformer>& transformer) :
        _tileId(tileId), _layerIndex(layerIdx), _localId(tileLabel.getLocalId()), _globalId(tileLabel.getGlobalId()), _groupId(tileLabel.getGroupId()), _glyphs(tileLabel.getGlyphs()), _variants(tileLabel.getVariants()), _style(tileLabel.getStyle()), _priority(tileLabel.getPlacementInfo().priority), _minimumGroupDistance(tileLabel.getPlacementInfo().minimumGroupDistance), _allowOverlapSameFeatureId(tileLabel.getPlacementInfo().allowOverlapSameFeatureId), _sameFeatureIdDependent(tileLabel.getPlacementInfo().sameFeatureIdDependent), _geoPointIndex(tileLabel.getGeoPointIndex())
    {
        _cachedVertices.reserve(_glyphs.size() * 4);
        _cachedTexCoords.reserve(_glyphs.size() * 4);
        _cachedAttribs.reserve(_glyphs.size() * 4);
        _cachedIndices.reserve(_glyphs.size() * 6);

        measureTextLines();

        // One box per side the text may be laid out on; the culler switches between them and the
        // envelope, the plate and the frustum test all read the one in use.
        _variantBBoxes.reserve(_variants.size());
        _variantTextBBoxes.reserve(_variants.size());
        for (const TileLabel::Variant& variant : _variants) {
            _variantBBoxes.push_back(calculateGlyphBBox(variant.shift, variant.drawText, Part::ALL, variant.lineAlign));
            _variantTextBBoxes.push_back(calculateGlyphBBox(variant.shift, variant.drawText, Part::TEXT, variant.lineAlign));
        }
        _glyphBBox = (_variants.empty() ? calculateGlyphBBox(cglib::vec2<float>(0, 0), true, Part::ALL, _style->textLineAlign) : _variantBBoxes[0]);
        _textBBox = (_variants.empty() ? calculateGlyphBBox(cglib::vec2<float>(0, 0), true, Part::TEXT, _style->textLineAlign) : _variantTextBBoxes[0]);
        // The icon run is before the first line break, so no variant ever moves it.
        _iconBBox = calculateGlyphBBox(cglib::vec2<float>(0, 0), true, Part::ICON);

        // How far the glyphs reach from the anchor, with the style transform applied the same
        // way findClippedPointPlacement applies it. updatePlacement grows the label's geometry
        // bounds by this before testing them against the frustum, so an anchor that sits just
        // outside the view but whose text reaches into it is not rejected. Every side counts here:
        // the culler may move the text to any of them AFTER this test has let the label through.
        for (std::size_t i = 0; i < std::max<std::size_t>(1, _variantBBoxes.size()); i++) {
            cglib::bbox2<float> glyphBBox = (_variantBBoxes.empty() ? _glyphBBox : _variantBBoxes[i]);
            if (glyphBBox.min(0) > glyphBBox.max(0)) {
                continue;
            }
            if (_style->transform) {
                cglib::mat2x2<float> transform = _style->transform->matrix2();
                std::array<cglib::vec2<float>, 4> envelope;
                envelope[0] = cglib::transform(cglib::vec2<float>(glyphBBox.min(0), glyphBBox.min(1)), transform);
                envelope[1] = cglib::transform(cglib::vec2<float>(glyphBBox.min(0), glyphBBox.max(1)), transform);
                envelope[2] = cglib::transform(cglib::vec2<float>(glyphBBox.max(0), glyphBBox.min(1)), transform);
                envelope[3] = cglib::transform(cglib::vec2<float>(glyphBBox.max(0), glyphBBox.max(1)), transform);
                glyphBBox = cglib::bbox2<float>::make_union(envelope.begin(), envelope.end());
            }
            _maxGlyphExtent = std::max(_maxGlyphExtent,
                                       std::max(std::max(std::abs(glyphBBox.min(0)), std::abs(glyphBBox.max(0))),
                                                std::max(std::abs(glyphBBox.min(1)), std::abs(glyphBBox.max(1)))));
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

    void Label::measureTextLines() {
        cglib::vec2<float> pen(0, 0);
        bool text = false;
        float lineMin = 0, lineMax = 0;
        bool lineEmpty = true;
        auto flushLine = [&]() {
            if (!lineEmpty) {
                _lineExtents.emplace_back(lineMin, lineMax);
            }
            else if (text) {
                _lineExtents.emplace_back(0.0f, 0.0f);
            }
            lineEmpty = true;
        };
        for (const Font::Glyph& glyph : _glyphs) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                flushLine();
                pen = cglib::vec2<float>(0, 0);
                text = true;
            }
            else if (text) {
                float x0 = pen(0) + glyph.offset(0), x1 = x0 + glyph.size(0);
                if (lineEmpty) {
                    lineMin = x0; lineMax = x1; lineEmpty = false;
                }
                else {
                    lineMin = std::min(lineMin, x0); lineMax = std::max(lineMax, x1);
                }
            }
            pen += glyph.advance;
        }
        flushLine();

        bool first = true;
        for (const cglib::vec2<float>& extent : _lineExtents) {
            if (extent(0) == extent(1)) {
                continue; // an empty line has no ink to align
            }
            _blockExtent = (first ? extent : cglib::vec2<float>(std::min(_blockExtent(0), extent(0)), std::max(_blockExtent(1), extent(1))));
            first = false;
        }
    }

    float Label::calculateLineShift(std::size_t lineIndex, float lineAlign) const {
        if (lineAlign == 0.0f || lineIndex >= _lineExtents.size() || _lineExtents.size() < 2) {
            return 0.0f;
        }
        const cglib::vec2<float>& extent = _lineExtents[lineIndex];
        if (extent(0) == extent(1)) {
            return 0.0f;
        }
        return (lineAlign > 0 ? _blockExtent(1) - extent(1) : _blockExtent(0) - extent(0)) * std::abs(lineAlign);
    }

    cglib::bbox2<float> Label::calculateGlyphBBox(const cglib::vec2<float>& shift, bool drawText, Part part, float lineAlign) const {
        cglib::bbox2<float> bbox = cglib::bbox2<float>::smallest();
        walkGlyphs(shift, drawText, lineAlign, [&bbox, part](const Font::Glyph& glyph, const cglib::vec2<float>& pen, bool text) {
            if (part == Part::ALL || (text ? part == Part::TEXT : part == Part::ICON)) {
                bbox.add(pen + glyph.offset);
                bbox.add(pen + glyph.offset + glyph.size);
            }
        });
        return bbox;
    }

    cglib::bbox2<float> Label::calculatePlatedBBox(int variantIndex, float glyphScale) const {
        bool indexed = variantIndex >= 0 && variantIndex < static_cast<int>(_variantBBoxes.size());
        cglib::bbox2<float> bbox = (indexed ? _variantBBoxes[variantIndex] : _glyphBBox);
        const cglib::bbox2<float>& textBBox = (indexed ? _variantTextBBoxes[variantIndex] : _textBBox);
        auto expand = [&bbox, glyphScale](const cglib::bbox2<float>& part, const TileLabel::Style::Plate& plate) {
            if (!plate.draws() || part.min(0) > part.max(0)) {
                return;
            }
            cglib::vec2<float> grow = plate.style.padding;
            if (plate.drawsBorder()) {
                grow += cglib::vec2<float>(plate.style.borderWidth, plate.style.borderWidth);
            }
            grow *= glyphScale;
            bbox.add(part.min - grow);
            bbox.add(part.max + grow);
        };
        expand(textBBox, _style->textPlate);
        expand(_iconBBox, _style->iconPlate);
        return bbox;
    }

    void Label::setVariantIndex(int index) {
        if (index == _variantIndex || _variants.empty()) {
            return;
        }
        _variantIndex = std::max(0, std::min(static_cast<int>(_variants.size()) - 1, index));
        _glyphBBox = _variantBBoxes[_variantIndex];
        _textBBox = _variantTextBBoxes[_variantIndex];
        _cachedValid = false; // the quads are laid out around the pen, so they have to be rebuilt
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

    void Label::smoothPlacementLine(const std::vector<cglib::vec3<double>>& vertices, std::size_t index, double minEdgeLength, std::vector<cglib::vec3<double>>& smoothedVertices, std::size_t& smoothedIndex) {
        // Glyphs are laid out edge by edge, so a line whose edges are shorter than the glyphs
        // turns every bit of its own noise into a turn of the text - and the direction test in
        // buildLineVertexData then rejects the placement. That is what happens to a contour
        // traced on a DEM grid: it runs along cell edges, so it zigzags by a whole cell on every
        // step. Averaging the vertices over a window of the given length (rather than picking
        // every n-th one, which samples the zigzag instead of removing it) leaves the direction
        // the line actually takes. Only the placement is smoothed; the geometry keeps being
        // drawn from its own vertices.
        smoothedVertices.clear();
        smoothedIndex = index;
        if (vertices.size() < 2 || !(minEdgeLength > 0)) {
            smoothedVertices = vertices;
            return;
        }

        std::vector<std::size_t> sourceIndices;
        cglib::vec3<double> sum = vertices.front();
        std::size_t count = 1;
        double length = 0;
        for (std::size_t i = 1; i < vertices.size(); i++) {
            length += cglib::length(vertices[i] - vertices[i - 1]);
            sum += vertices[i];
            count++;
            if (length >= minEdgeLength || i + 1 == vertices.size()) {
                smoothedVertices.push_back(sum * (1.0 / count));
                sourceIndices.push_back(i);
                sum = cglib::vec3<double>(0, 0, 0);
                count = 0;
                length = 0;
            }
        }
        if (smoothedVertices.size() < 2) {
            smoothedVertices = vertices;
            smoothedIndex = index;
            return;
        }

        // The anchor lies on source segment [index, index + 1]. Each smoothed vertex averages one
        // window of source vertices ending at sourceIndices[i], so the anchor belongs to the first
        // window reaching past it - and the edge to lay the glyphs along starts there.
        smoothedIndex = smoothedVertices.size() - 2;
        for (std::size_t i = 0; i < sourceIndices.size(); i++) {
            if (index <= sourceIndices[i]) {
                smoothedIndex = std::min(i, smoothedVertices.size() - 2);
                break;
            }
        }
    }

    void Label::clampPlacementAnchor(const std::vector<cglib::vec3<double>>& vertices, double textLength, std::size_t& index, cglib::vec3<double>& position) {
        // The glyphs are laid out FORWARD from the anchor (and backwards from it when the label is
        // flipped to stay readable), so an anchor sitting near an end of the line has no room and
        // the placement is dropped. Anchors are generated per segment without knowing how much
        // line follows, and a line short enough to have no room anywhere - a contour ring cut into
        // fragments, for instance - would never label at all. Slide the anchor along its own line
        // until the run fits, staying as close to where it was as possible.
        if (vertices.size() < 2 || !(textLength > 0)) {
            return;
        }

        std::vector<double> lengths(vertices.size(), 0);
        for (std::size_t i = 1; i < vertices.size(); i++) {
            lengths[i] = lengths[i - 1] + cglib::length(vertices[i] - vertices[i - 1]);
        }
        double total = lengths.back();
        if (!(total > 0)) {
            return;
        }

        index = std::min(index, vertices.size() - 2);
        double anchor = lengths[index] + cglib::length(position - vertices[index]);
        // A line shorter than two runs can not have room on both sides; its middle is the best
        // compromise, and the flipped placement then fails on the side that is too short.
        // The run needs a little more room than its own length: the glyphs are fitted edge by
        // edge and each fit consumes slightly more line than the advance it stands for.
        double room = textLength * PLACEMENT_ROOM_FACTOR;
        double minAnchor = std::min(room, total * 0.5);
        double maxAnchor = std::max(total - room, total * 0.5);
        double clamped = std::min(std::max(anchor, minAnchor), maxAnchor);
        if (clamped == anchor) {
            return;
        }

        std::size_t i = 0;
        while (i + 2 < vertices.size() && lengths[i + 1] < clamped) {
            i++;
        }
        double edgeLength = lengths[i + 1] - lengths[i];
        double t = (edgeLength > 0 ? (clamped - lengths[i]) / edgeLength : 0);
        index = i;
        position = vertices[i] + (vertices[i + 1] - vertices[i]) * std::min(1.0, std::max(0.0, t));
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
        std::vector<cglib::vec3<double>> smoothedVertices;
        std::size_t smoothedIndex = index;
        smoothPlacementLine(tileLine.vertices, index, _placementTextLength * PLACEMENT_SMOOTH_TEXT_FRACTION, smoothedVertices, smoothedIndex);
        clampPlacementAnchor(smoothedVertices, _placementTextLength, smoothedIndex, pos);
        return std::make_shared<const Placement>(tileLine.tileId, tileLine.localId, smoothedVertices, smoothedIndex, index, pos, tileLine.normal);
    }

    void Label::snapPlacement(const Label& label) {
        _placement = label._placement;
        _cachedFlippedPlacement = label._cachedFlippedPlacement;
        // The re-snapped placement below is rebuilt right here, before this label ever sees a
        // view state - it has to smooth its line by the same length the old one did, or the
        // glyph run changes shape on every tile-set change.
        _placementTextLength = label._placementTextLength;
        // A label recreated by a tile-set change is the same label to the user: it keeps the
        // allowance its run already earned, or it would be re-judged strictly (and blink) every
        // time tiles stream in.
        _lineLayoutValid = label._lineLayoutValid;
        // The same goes for where a callout was lifted to: the offset belongs to the label, not to
        // the tiles it was built from, and a rebuilt label that starts at 0 drops onto its own
        // anchor until the next placement pass - which is a whole screen of names jumping every
        // time tiles stream in while panning.
        _calloutOffset = label._calloutOffset;
        _calloutAnchorScreenY = label._calloutAnchorScreenY;
        _calloutAnchored = label._calloutAnchored;
        _calloutFailures = label._calloutFailures;
        // And the side its text is on: the culler re-tests it every pass anyway, but a rebuilt
        // label that starts at the first side draws one frame there before the next pass moves it
        // back - which is the name hopping around its icon while tiles stream in.
        setVariantIndex(label._variantIndex);
        if (!_placement) {
            return;
        }
#if MASSIF_VT_RENDER_STATS
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

#if MASSIF_VT_RENDER_STATS
        if (_placement) {
            double dx = _placement->position(0) - oldPosition(0);
            double dy = _placement->position(1) - oldPosition(1);
            if (dx * dx + dy * dy > SNAP_MOVE_EPSILON * SNAP_MOVE_EPSILON) {
                VT_STAT_INC(snapPlacementsMoved);
            }
        }
#endif
    }

    bool Label::hasGeometryOverTile(const TileId& tileId) const {
        // Elevation tiles and label tiles are different tile sets at different zooms, so the
        // test is 'the two tiles overlap on the ground', not equality.
        for (const TilePoint& tilePoint : _tilePoints) {
            if (tilePoint.tileId.getWrapped().intersects(tileId)) {
                return true;
            }
        }
        for (const TileLine& tileLine : _tileLines) {
            if (tileLine.tileId.getWrapped().intersects(tileId)) {
                return true;
            }
        }
        return false;
    }

    void Label::updateElevation(const std::function<double(const cglib::vec3<double>&)>& heightFunc) {
        // Refresh anchor heights from the elevation data. Label geometry is built when the
        // tile is decoded, possibly before its elevation data has arrived - this re-anchors
        // the labels onto the terrain when the elevation version changes. A line placement
        // is rebuilt from the re-anchored line (below) rather than just shifted, so the
        // glyph run keeps following the terrain profile it is drawn over.
        bool changed = false;
        _elevationAnchored = true;
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
                if (_placement->sourceIndex + 1 < tileLine.vertices.size()) {
                    placement = buildLinePlacement(tileLine, _placement->sourceIndex, position);
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

        // Refresh the length of the glyph run for the placements built below: it is a screen
        // size, so the distance it covers depends on the view - including the terrain factor
        // that keeps labels the same size on screen, which the layout below is measured against
        // (calculateEnvelope applies it too). Without it, a label over terrain reserves the
        // wrong amount of line and its run then walks off the end.
        if (isLineRun()) {
            float scale = (_style->sizeFunc)(viewState) * viewState.zoomScale * _style->scale;
            // Measured at the placement itself, not at the center of the label's geometry: the
            // geometry is the feature merged over every tile holding it, so its center can be far
            // from where the label sits - and the terrain factor, which is what the run is
            // measured with when it is laid out, changes with that distance.
            scale *= calculateTerrainScaleFactor(_placement ? _placement->position : calculateGeometryBBox(viewState).center(), viewState);
            float textLength = 0;
            for (const Font::Glyph& glyph : _glyphs) {
                textLength += glyph.advance(0) * scale;
            }
            _placementTextLength = textLength;
        }
        if (_placement) {
            std::array<cglib::vec3<float>, 4> envelope;
            calculateEnvelope(viewState, envelope);
            cglib::bbox3<double> bbox = cglib::bbox3<double>::smallest();
            for (const cglib::vec3<float>& pos : envelope) {
                bbox.add(viewState.origin + cglib::vec3<double>::convert(pos));
            }
            // A line placement is only worth keeping while the glyph run can be laid out on it.
            // The run follows the PROJECTED line, whose length changes with the camera - a stretch
            // of road that carried the text a moment ago can be foreshortened to half of it - and
            // keeping such a placement only hides the label, on a line that may well have another
            // piece able to carry it. calculateEnvelope above has just laid the run out at this
            // view, so its verdict is the current one.
            if (viewState.frustum.inside(bbox) && (!isLineRun() || _lineLayoutValid)) {
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

        // Nothing of this label is in view, and the loaded tile set reaches well past the viewport,
        // so this is most of a frame's placement work. The placement is DROPPED rather than kept:
        // an invalid label is what excludes it from the culler, and one holding a placement off
        // screen would keep claiming (clamped) border grid cells from visible labels.
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
        return calculateTerrainScaleFactor(placement.position, viewState);
    }

    float Label::calculateTerrainScaleFactor(const cglib::vec3<double>& position, const ViewState& viewState) const {
        // In a planar projection labels keep a CONSTANT ON-SCREEN SIZE (tangram-style): the
        // label world size is derived from the zoom level only, so the perspective divide would
        // otherwise scale labels by their distance - oversizing labels lifted onto high mountains,
        // shrinking them towards the horizon, and on a tilted 2D map making the nearest ones far
        // too big. Rescale by the ratio of the label view depth to the distance the zoom is
        // calibrated at - the camera-to-focus distance - which exactly cancels the perspective
        // scaling.
        if (!viewState.planarProjection) {
            return 1.0f;
        }
        cglib::vec3<double> viewDir = -cglib::vec3<double>::convert(viewState.orientation[2]);
        double depth = cglib::dot_product(position - viewState.origin, viewDir);
        if (!(depth > 0)) {
            return 1.0f;
        }
        // Where the view axis meets the ground is only the same thing while the focus point sits
        // ON the ground: raise the viewpoint or aim at the horizon and it grows without bound,
        // shrinking every label (which is exactly what a panorama does).
        double focusDepth = (viewState.focusDistance > 0 ? viewState.focusDistance
                                                         : (viewDir(2) < 0 ? viewState.origin(2) / -viewDir(2) : depth));
        if (!(focusDepth > 0)) {
            return 1.0f;
        }
        float factor = static_cast<float>(depth / focusDepth);
        // Quantize to ~1% steps: line label vertex data is cached by scale and would
        // otherwise be rebuilt on every frame while the camera moves
        factor = std::exp2(std::round(std::log2(factor) * 64.0f) / 64.0f);
        // The cap is what makes a label shrink again once it is very far away. In a panorama that
        // is most of the frame - the focus is a few km out and the horizon a hundred - and a
        // callout labelling a summit at that range is the whole point of the view, so it keeps its
        // size all the way out.
        float maxFactor = (_style->orientation == LabelOrientation::CALLOUT ? 4096.0f : 8.0f);
        return std::min(maxFactor, std::max(0.05f, factor));
    }

    bool Label::calculateEnvelope(float size, float buffer, const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const {
        std::shared_ptr<const Placement> placement = getPlacement(viewState);
        float scale = calculateLabelScale(size, viewState, placement);
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
        if (_style->orientation == LabelOrientation::CALLOUT && size > 0) {
            // The lift the culler decided on, plus the slide that puts the style's line anchor
            // over the feature. One pixel is scale/size world units (the glyph quads are in units
            // of the font size), and the envelope has to move with the glyphs or the collision
            // test is done where the label is not.
            float pixelScale = scale / size;
            cglib::vec2<float> calloutShift = calculateCalloutShift(scale, 1.0f / size)
                + cglib::vec2<float>(0, calculateCalloutLift(viewState) * calculatePixelToWorld(viewState, *placement, pixelScale));
            origin = origin + xAxis * calloutShift(0) + yAxis * calloutShift(1);
        }

        bool valid = isSurfaceFacingView(viewState, *placement);
        if (isLineRun()) {
            // The run is laid out in glyph units on the label's own axes (see buildLineVertexData),
            // so its envelope is the bounds of that run put back on them - the same shape the
            // renderer draws. The envelope serves collision, so it takes the layout that is there
            // rather than re-laying the run out for this caller's view (see updateLineVertexData).
            updateLineVertexData(placement, scale, viewState, false);

            float glyphPadding = (scale > 0 ? padding / scale : 0);
            float minX = std::numeric_limits<float>::max(), maxX = -std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max(), maxY = -std::numeric_limits<float>::max();
            for (const cglib::vec3<float>& vertex : _cachedVertices) {
                minX = std::min(minX, vertex(0)); maxX = std::max(maxX, vertex(0));
                minY = std::min(minY, vertex(1)); maxY = std::max(maxY, vertex(1));
            }
            if (minX > maxX) {
                minX = maxX = minY = maxY = 0;
            }
            minX -= glyphPadding; maxX += glyphPadding;
            minY -= glyphPadding; maxY += glyphPadding;

            envelope[0] = origin + xAxis * (minX * scale) + yAxis * (minY * scale);
            envelope[1] = origin + xAxis * (maxX * scale) + yAxis * (minY * scale);
            envelope[2] = origin + xAxis * (maxX * scale) + yAxis * (maxY * scale);
            envelope[3] = origin + xAxis * (minX * scale) + yAxis * (maxY * scale);

            valid = valid && _cachedValid;
        }
        else {
            // Use bounding box for envelope. The plates are part of what the label covers, so what
            // they add around the glyphs belongs here too - the band aligns labels on this box, and
            // a box smaller than what is drawn puts the row a few pixels off.
            cglib::bbox2<float> box = (size > 0 ? calculatePlatedBBox(_variantIndex, 1.0f / size) : _glyphBBox);
            buildBoxEnvelope(box, scale, cglib::vec2<float>(padding, padding), origin, xAxis, yAxis, envelope);
        }
        return valid;
    }

    void Label::buildBoxEnvelope(const cglib::bbox2<float>& glyphBBox, float scale, const cglib::vec2<float>& padding, const cglib::vec3<float>& origin, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis, std::array<cglib::vec3<float>, 4>& envelope) const {
        float minX = glyphBBox.min(0) * scale - padding(0), maxX = glyphBBox.max(0) * scale + padding(0);
        float minY = glyphBBox.min(1) * scale - padding(1), maxY = glyphBBox.max(1) * scale + padding(1);
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

    bool Label::calculateVariantEnvelopes(float size, float buffer, const ViewState& viewState, std::vector<std::array<cglib::vec3<float>, 4>>& envelopes) const {
        // Only a point label carries variants: a LINE label's envelope comes from the laid-out run
        // and a CALLOUT has a placement search of its own.
        if (_variantBBoxes.empty() || isLineRun() || _style->orientation == LabelOrientation::CALLOUT) {
            envelopes.resize(1);
            return calculateEnvelope(size, buffer, viewState, envelopes[0]);
        }

        envelopes.resize(_variantBBoxes.size());
        std::shared_ptr<const Placement> placement = getPlacement(viewState);
        float scale = calculateLabelScale(size, viewState, placement);
        if (!placement || scale <= 0) {
            cglib::vec3<float> origin(0, 0, static_cast<float>(-viewState.origin(2)));
            for (std::array<cglib::vec3<float>, 4>& envelope : envelopes) {
                envelope.fill(origin);
            }
            return false;
        }

        float padding = buffer * viewState.zoomScale * _style->scale * calculateTerrainScaleFactor(*placement, viewState) / std::sqrt(2.0f);
        float glyphScale = (size > 0 ? 1.0f / size : 0.0f);
        cglib::vec3<float> origin, xAxis, yAxis;
        setupCoordinateSystem(viewState, placement, origin, xAxis, yAxis);
        for (std::size_t i = 0; i < _variantBBoxes.size(); i++) {
            cglib::bbox2<float> box = (glyphScale > 0 ? calculatePlatedBBox(static_cast<int>(i), glyphScale) : _variantBBoxes[i]);
            buildBoxEnvelope(box, scale, cglib::vec2<float>(padding, padding), origin, xAxis, yAxis, envelopes[i]);
        }
        return isSurfaceFacingView(viewState, *placement);
    }

    bool Label::isSurfaceFacingView(const ViewState& viewState, const Placement& placement) const {
        // A CALLOUT is a screen object: it faces the camera, it is lifted along the camera up axis
        // and it is joined to its feature by a leader line, so how steeply the view meets the
        // ground it is anchored on says nothing about whether it can be read. Every other
        // orientation is laid out against that surface and does degenerate as it is seen edge-on -
        // and a panorama, which is what callouts are for, is that view by definition.
        if (_style->orientation == LabelOrientation::CALLOUT) {
            return true;
        }
        return cglib::dot_product(viewState.orientation[2], placement.normal) > MIN_BILLBOARD_VIEW_NORMAL_DOTPRODUCT;
    }

    bool Label::calculateVertexData(float size, const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices, DrawPass pass, const LabelPlateIndices& plates, int secondaryStyleIndex, int iconStyleIndex) const {
        VT_STAT_CLOCK(labelClock);
        std::shared_ptr<const Placement> placement = getPlacement(viewState);
        VT_STAT_SPLIT(labelPlacementNs, labelClock);
        float scale = calculateLabelScale(size, viewState, placement);
        if (!placement || scale <= 0) {
            return false;
        }

        // Build vertex data cache
        bool valid = isSurfaceFacingView(viewState, *placement);
        if (pass == DrawPass::CALLOUT_LINE) {
            appendCalloutLine(size, scale, viewState, placement, styleIndex, vertices, offsets, normals, texCoords, attribs, indices);
            return valid;
        }
        // Which frame the offsets below are expressed in; the shader reads it from attribs[3].
        std::int8_t billboardMode = CAMERA_AXIS_OFFSET;
        if (isLineRun()) {
            // The drawn run has to follow the line as THIS view projects it - this is the caller
            // that rebuilds it (see updateLineVertexData).
            updateLineVertexData(placement, scale, viewState, true);
            VT_STAT_SPLIT(labelLineBuildNs, labelClock);
            if (_cachedVertices.size() > MAX_LABEL_VERTICES) {
                return false;
            }

            cglib::vec3<float> origin, xAxis, yAxis;
            setupCoordinateSystem(viewState, placement, origin, xAxis, yAxis);
            vertices.fill(origin, _cachedVertices.size());
            if (isScreenLineRun()) {
                // Laid out on the camera axes, so the shader can span it from them: emit the
                // anchor and the run-local offset and let uLabelAxisX/Y do the rest.
                for (const cglib::vec3<float>& vertex : _cachedVertices) {
                    offsets.append(cglib::vec3<float>(vertex(0) * scale, vertex(1) * scale, 0));
                }
            }
            else {
                // Flat on the surface: the run was laid out on the placement's own tangent frame,
                // so span it here and hand the shader a world offset.
                billboardMode = WORLD_OFFSET;
                for (const cglib::vec3<float>& vertex : _cachedVertices) {
                    offsets.append(xAxis * (vertex(0) * scale) + yAxis * (vertex(1) * scale));
                }
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
            // The plates first: within one label the draw order is the index order, so anything
            // appended after the glyphs would cover them.
            cglib::vec2<float> calloutShift(0, 0);
            if (_style->orientation == LabelOrientation::CALLOUT && size > 0) {
                float pixelScale = scale / size;
                calloutShift = calculateCalloutShift(scale, 1.0f / size)
                    + cglib::vec2<float>(0, calculateCalloutLift(viewState) * calculatePixelToWorld(viewState, *placement, pixelScale));
            }
            appendLabelPlates(size, scale, placement, plates, calloutShift, origin, xAxis, yAxis, vertices, offsets, normals, texCoords, attribs, indices);
            vertices.fill(origin, _cachedVertices.size());
            if (_style->orientation == LabelOrientation::BILLBOARD_3D || _style->orientation == LabelOrientation::LINE_BILLBOARD_3D || _style->orientation == LabelOrientation::CALLOUT) {
                // Axes are the camera's: leave them to the shader (see labelVsh). A callout is
                // lifted along the camera up axis by what the culler decided (see
                // setCalloutOffset) and slid sideways so that the style's line anchor sits over
                // the feature; the anchor itself stays put, which is where its leader line starts.
                for (const cglib::vec3<float>& vertex : _cachedVertices) {
                    offsets.append(cglib::vec3<float>(vertex(0) * scale + calloutShift(0), vertex(1) * scale + calloutShift(1), 0));
                }
            } else {
                // Axes come from the placement (or from the placement normal and the camera
                // up vector) - span the offset here and hand the shader a world offset.
                billboardMode = WORLD_OFFSET;
                for (const cglib::vec3<float>& vertex : _cachedVertices) {
                    offsets.append(xAxis * (vertex(0) * scale) + yAxis * (vertex(1) * scale));
                }
            }
        }

        VT_STAT_SPLIT(labelTransformNs, labelClock);
        normals.fill(placement->normal, _cachedVertices.size());
        texCoords.copy(_cachedTexCoords, 0, _cachedTexCoords.size());

        if (haloStyleIndex >= 0) {
            for (const cglib::vec4<std::int8_t>& attrib : _cachedAttribs) {
                attribs.append(cglib::vec4<std::int8_t>(static_cast<std::int8_t>(haloStyleIndex), attrib(1), static_cast<std::int8_t>(_opacity * 127.0f), billboardMode));
            }
            
            std::uint16_t offset = static_cast<std::uint16_t>(vertices.size() - _cachedVertices.size());
            for (std::uint16_t idx : _cachedIndices) {
                indices.append(idx + offset);
            }

            vertices.copy(vertices, vertices.size() - _cachedVertices.size(), _cachedVertices.size());
            offsets.copy(offsets, offsets.size() - _cachedVertices.size(), _cachedVertices.size());

            normals.fill(placement->normal, _cachedVertices.size());
            texCoords.copy(_cachedTexCoords, 0, _cachedTexCoords.size());
        }

        for (const cglib::vec4<std::int8_t>& attrib : _cachedAttribs) {
            int glyphStyleIndex = styleIndex;
            if (attrib(0) == 1 && secondaryStyleIndex >= 0) {
                glyphStyleIndex = secondaryStyleIndex;
            }
            else if (attrib(0) == 2 && iconStyleIndex >= 0) {
                glyphStyleIndex = iconStyleIndex;
            }
            attribs.append(cglib::vec4<std::int8_t>(static_cast<std::int8_t>(glyphStyleIndex), attrib(1), static_cast<std::int8_t>(_opacity * 127.0f), billboardMode));
        }
        
        std::uint16_t offset = static_cast<std::uint16_t>(vertices.size() - _cachedVertices.size());
        for (std::uint16_t idx : _cachedIndices) {
            if (!(haloStyleIndex >= 0 && _cachedAttribs[idx](1) != 0)) { // do not add non-SDF glyphs if halo is enabled in the second pass
                indices.append(idx + offset);
            }
        }

        if (pass == DrawPass::ALL) {
            appendCalloutLine(size, scale, viewState, placement, styleIndex, vertices, offsets, normals, texCoords, attribs, indices);
        }

        VT_STAT_SPLIT(labelAttribNs, labelClock);
        return valid;
    }

    void Label::buildPointVertexData(VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        walkGlyphs(calculateVariantShift(), drawsText(), calculateLineAlign(), [&](const Font::Glyph& glyph, const cglib::vec2<float>& pen, bool) {
            if (glyph.codePoint == Font::SPACE_CODEPOINT) {
                return;
            }
            std::uint16_t i0 = static_cast<std::uint16_t>(vertices.size());
            indices.append(i0 + 0, i0 + 1, i0 + 2);
            indices.append(i0 + 0, i0 + 2, i0 + 3);

            std::int16_t u0 = static_cast<std::int16_t>(glyph.baseGlyph.x), u1 = static_cast<std::int16_t>(glyph.baseGlyph.x + glyph.baseGlyph.width);
            std::int16_t v0 = static_cast<std::int16_t>(glyph.baseGlyph.y), v1 = static_cast<std::int16_t>(glyph.baseGlyph.y + glyph.baseGlyph.height);
            texCoords.append(cglib::vec2<std::int16_t>(u0, v1), cglib::vec2<std::int16_t>(u1, v1), cglib::vec2<std::int16_t>(u1, v0), cglib::vec2<std::int16_t>(u0, v0));

            // attribs[0] carries the run the glyph belongs to until calculateVertexData turns
            // it into a style index: 1 = the second run, 2 = the icon run, both of which may
            // have their own colour.
            cglib::vec4<std::int8_t> attrib(glyph.icon ? 2 : (glyph.secondary ? 1 : 0), static_cast<std::int8_t>(glyph.baseGlyph.mode), 0, 0);
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
        });
    }

    void Label::appendLabelPlates(float size, float scale, const std::shared_ptr<const Placement>& placement, const LabelPlateIndices& plates, const cglib::vec2<float>& calloutShift, const cglib::vec3<float>& origin, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        if (!(size > 0)) {
            return;
        }
        float pixelScale = scale / size;
        bool cameraAxes = (_style->orientation == LabelOrientation::BILLBOARD_3D || _style->orientation == LabelOrientation::LINE_BILLBOARD_3D || _style->orientation == LabelOrientation::CALLOUT);

        // Border first, then fill on top of it, and the icon's plate before the text's: within one
        // label the draw order is the index order, so anything appended later covers what came
        // before - and all of this has to end up under the glyphs.
        struct Layer { const cglib::bbox2<float>* box; const TileLabel::Style::Plate* plate; int styleIndex; bool border; };
        const Layer layers[4] = {
            { &_iconBBox, &_style->iconPlate, plates.iconBorder, true },
            { &_iconBBox, &_style->iconPlate, plates.iconFill,   false },
            { &_textBBox, &_style->textPlate, plates.textBorder, true },
            { &_textBBox, &_style->textPlate, plates.textFill,   false }
        };
        for (const Layer& layer : layers) {
            if (layer.styleIndex < 0 || layer.box->min(0) > layer.box->max(0)) {
                continue;
            }
            const TileLabel::Style::Plate& plate = *layer.plate;
            const std::optional<GlyphMap::Glyph>& glyph = (layer.border ? plate.borderGlyph : plate.glyph);
            if (!glyph || (layer.border ? !plate.drawsBorder() : !plate.drawsFill())) {
                continue;
            }
            float grow = plate.style.borderWidth * (layer.border ? 1.0f : 0.0f);
            cglib::vec2<float> padding = (plate.style.padding + cglib::vec2<float>(grow, grow)) * pixelScale;
            appendPlate(*layer.box, *glyph, (plate.style.radius + grow) * pixelScale, padding, scale, layer.styleIndex, cameraAxes, calloutShift, origin, xAxis, yAxis, placement, vertices, offsets, normals, texCoords, attribs, indices);
        }
    }

    // 'radius' and 'grow' are already in the label's own units (screen pixels times the label
    // scale), like the glyph offsets around them.
    void Label::appendPlate(const cglib::bbox2<float>& box, const GlyphMap::Glyph& glyph, float radius, const cglib::vec2<float>& grow, float scale, int styleIndex, bool cameraAxes, const cglib::vec2<float>& calloutShift, const cglib::vec3<float>& origin, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis, const std::shared_ptr<const Placement>& placement, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        // The plate covers the box plus what it is grown by, both in glyph units (1 unit = the
        // font size), and is cut into three columns: the two caps keep the cell's corner radius,
        // the middle is stretched. That is what keeps a corner round on a long name.
        float x0 = box.min(0) * scale - grow(0), x1 = box.max(0) * scale + grow(0);
        float y0 = box.min(1) * scale - grow(1), y1 = box.max(1) * scale + grow(1);
        float height = y1 - y0;
        float cap = std::max(0.0f, std::min(radius, (x1 - x0) * 0.5f));

        // Atlas coordinates of the cell's left cap, middle column and right cap. Sampled one texel
        // INSIDE the cell on EVERY side: the outer texels blend into the transparent padding around
        // it under linear filtering, which is what made the plate's edges look soft. The cell keeps
        // a transparent margin of its own for this, so the inset lands on the shape's own edge
        // (see buildRoundedRectBitmap) rather than one column into it.
        float u0 = static_cast<float>(glyph.x + 1);
        float u1 = static_cast<float>(glyph.x + glyph.width - 2);
        float uMid = (u0 + u1) * 0.5f;
        float v0 = static_cast<float>(glyph.y + 1);
        float v1 = static_cast<float>(glyph.y + glyph.height - 2);

        struct Slice { float x0, x1, u0, u1; };
        const Slice slices[3] = {
            { x0, x0 + cap, u0, uMid },
            { x0 + cap, x1 - cap, uMid - 0.5f, uMid + 0.5f },
            { x1 - cap, x1, uMid, u1 }
        };

        cglib::mat2x2<float> transform = (_style->transform ? _style->transform->matrix2() : cglib::mat2x2<float>::identity());
        for (int i = 0; i < 3; i++) {
            const Slice& slice = slices[i];
            if (!(slice.x1 > slice.x0)) {
                continue;
            }
            std::uint16_t i0 = static_cast<std::uint16_t>(vertices.size());
            indices.append(i0 + 0, i0 + 1, i0 + 2);
            indices.append(i0 + 0, i0 + 2, i0 + 3);

            std::int16_t su0 = static_cast<std::int16_t>(slice.u0), su1 = static_cast<std::int16_t>(slice.u1);
            std::int16_t sv0 = static_cast<std::int16_t>(v0), sv1 = static_cast<std::int16_t>(v1);
            texCoords.append(cglib::vec2<std::int16_t>(su0, sv1), cglib::vec2<std::int16_t>(su1, sv1), cglib::vec2<std::int16_t>(su1, sv0), cglib::vec2<std::int16_t>(su0, sv0));

            cglib::vec4<std::int8_t> attrib(static_cast<std::int8_t>(styleIndex), static_cast<std::int8_t>(GlyphMap::GlyphMode::BITMAP), static_cast<std::int8_t>(_opacity * 127.0f), cameraAxes ? CAMERA_AXIS_OFFSET : WORLD_OFFSET);
            attribs.append(attrib, attrib, attrib, attrib);

            const cglib::vec2<float> corners[4] = {
                cglib::vec2<float>(slice.x0, y0), cglib::vec2<float>(slice.x1, y0),
                cglib::vec2<float>(slice.x1, y0 + height), cglib::vec2<float>(slice.x0, y0 + height)
            };
            vertices.fill(origin, 4);
            normals.fill(placement->normal, 4);
            for (int c = 0; c < 4; c++) {
                cglib::vec2<float> p = cglib::transform(corners[c], transform);
                if (cameraAxes) {
                    offsets.append(cglib::vec3<float>(p(0) + calloutShift(0), p(1) + calloutShift(1), 0));
                } else {
                    offsets.append(xAxis * p(0) + yAxis * p(1));
                }
            }
        }
    }

    void Label::appendCalloutLine(float size, float scale, const ViewState& viewState, const std::shared_ptr<const Placement>& placement, int styleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        if (_style->orientation != LabelOrientation::CALLOUT || !_style->calloutLineGlyph || !(_style->calloutLineWidth > 0) || !(size > 0)) {
            return;
        }

        VertexArray<cglib::vec3<float>> lineOffsets;
        VertexArray<cglib::vec2<std::int16_t>> lineTexCoords;
        VertexArray<cglib::vec4<std::int8_t>> lineAttribs;
        VertexArray<std::uint16_t> lineIndices;
        buildCalloutLineVertexData(calculateCalloutLift(viewState), calculatePixelToWorld(viewState, *placement, scale / size), lineOffsets, lineTexCoords, lineAttribs, lineIndices);
        if (lineOffsets.empty()) {
            return;
        }

        std::uint16_t indexOffset = static_cast<std::uint16_t>(vertices.size());
        cglib::vec3<float> lineOrigin, lineXAxis, lineYAxis;
        setupCoordinateSystem(viewState, placement, lineOrigin, lineXAxis, lineYAxis);
        vertices.fill(lineOrigin, lineOffsets.size());
        offsets.copy(lineOffsets, 0, lineOffsets.size());
        normals.fill(placement->normal, lineOffsets.size());
        texCoords.copy(lineTexCoords, 0, lineTexCoords.size());
        for (const cglib::vec4<std::int8_t>& attrib : lineAttribs) {
            attribs.append(cglib::vec4<std::int8_t>(static_cast<std::int8_t>(styleIndex), attrib(1), static_cast<std::int8_t>(_opacity * 127.0f), CAMERA_AXIS_OFFSET));
        }
        for (std::uint16_t idx : lineIndices) {
            indices.append(idx + indexOffset);
        }
    }

    void Label::buildCalloutLineVertexData(float calloutLift, float pixelScale, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        const std::optional<GlyphMap::Glyph>& lineGlyph = _style->calloutLineGlyph;
        // The line runs from the anchor to the point of the label the style attaches it to (the
        // centre by default, the bottom left corner for a tilted panorama name), so a label
        // sitting on its own anchor has nothing to draw.
        float lift = calloutLift * pixelScale;
        if (!lineGlyph || !(lift > 0)) {
            return;
        }
        float halfWidth = _style->calloutLineWidth * pixelScale * 0.5f;

        std::uint16_t i0 = static_cast<std::uint16_t>(vertices.size());
        indices.append(i0 + 0, i0 + 1, i0 + 2);
        indices.append(i0 + 0, i0 + 2, i0 + 3);

        // Sample the interior of the atlas cell: the outer texels blend into the transparent
        // padding around it under linear filtering, which thins the line and fades its ends.
        std::int16_t u0 = static_cast<std::int16_t>(lineGlyph->x + 1), u1 = static_cast<std::int16_t>(lineGlyph->x + lineGlyph->width - 1);
        std::int16_t v0 = static_cast<std::int16_t>(lineGlyph->y + 1), v1 = static_cast<std::int16_t>(lineGlyph->y + lineGlyph->height - 1);
        texCoords.append(cglib::vec2<std::int16_t>(u0, v1), cglib::vec2<std::int16_t>(u1, v1), cglib::vec2<std::int16_t>(u1, v0), cglib::vec2<std::int16_t>(u0, v0));

        cglib::vec4<std::int8_t> attrib(0, static_cast<std::int8_t>(GlyphMap::GlyphMode::BITMAP), 0, 0);
        attribs.append(attrib, attrib, attrib, attrib);

        vertices.append(cglib::vec3<float>(-halfWidth, 0, 0), cglib::vec3<float>(halfWidth, 0, 0), cglib::vec3<float>(halfWidth, lift, 0), cglib::vec3<float>(-halfWidth, lift, 0));
    }

    float Label::calculateLabelScale(float size, const ViewState& viewState, const std::shared_ptr<const Placement>& placement) const {
        if (!placement) {
            return 0.0f;
        }
        float zoomScale = size * viewState.zoomScale * _style->scale * calculateTerrainScaleFactor(*placement, viewState);
        if (_style->orientation != LabelOrientation::CALLOUT) {
            return zoomScale;
        }
        // A callout is a screen object: its size is what the style asks for in pixels, taken off
        // the projection (see calculatePixelToWorld) rather than off the zoom. The zoom-derived
        // scale only holds a constant screen size while the camera distance follows the zoom, and
        // free roam breaks that - lift the viewpoint or tilt and the names grow or shrink.
        return size * calculatePixelToWorld(viewState, *placement, zoomScale / std::max(size, 1.0f));
    }

    float Label::calculateAnchorScreenY(const ViewState& viewState) const {
        std::shared_ptr<const Placement> placement = getPlacement(viewState);
        if (!placement) {
            return 0.0f;
        }
        cglib::vec3<double> pos = placement->position - viewState.origin;
        cglib::mat4x4<double> cameraMatrix = viewState.cameraMatrix;
        for (int i = 0; i < 3; i++) {
            cameraMatrix(i, 3) = 0; // the position is already camera-relative
        }
        cglib::vec4<double> clipPos = cglib::transform(cglib::vec4<double>(pos(0), pos(1), pos(2), 1), viewState.projectionMatrix * cameraMatrix);
        if (!(clipPos(3) > 0)) {
            return 0.0f;
        }
        return static_cast<float>((clipPos(1) / clipPos(3) * 0.5 + 0.5) * viewState.resolution);
    }

    float Label::calculateCalloutLift(const ViewState& viewState) const {
        if (!_calloutAnchored) {
            return _calloutOffset;
        }
        float anchorScreenY = calculateAnchorScreenY(viewState);
        if (anchorScreenY == 0.0f) {
            return _calloutOffset;
        }
        // The label holds its LINE, not its distance from a summit that has meanwhile moved.
        return _calloutOffset + (_calloutAnchorScreenY - anchorScreenY);
    }

    float Label::calculatePixelToWorld(const ViewState& viewState, const Placement& placement, float fallback) const {
        // One screen pixel is depth / (projection scale * half the screen height) world units at
        // that depth. Taking it from the projection rather than from the label's own scale is what
        // makes a lift in pixels MEAN pixels: the scale is derived from the zoom, so a camera that
        // tilts or rises changes what one unit of it is worth and the label slides up or down the
        // screen between placement passes.
        cglib::vec3<double> viewDir = -cglib::vec3<double>::convert(viewState.orientation[2]);
        double depth = cglib::dot_product(placement.position - viewState.origin, viewDir);
        double halfScreen = viewState.projectionMatrix(1, 1) * viewState.resolution * 0.5;
        if (!(depth > 0) || !(halfScreen > 0)) {
            return fallback;
        }
        return static_cast<float>(depth / halfScreen);
    }

    cglib::vec2<float> Label::calculateCalloutShift(float scale, float glyphScale) const {
        // The style names a point OF THE LABEL and that point is what the callout holds over the
        // feature: the label moves so that it lands on the anchor's vertical, which is what keeps
        // every leader line vertical and lets a tilted name start exactly above its summit.
        if (_style->orientation != LabelOrientation::CALLOUT || !_style->calloutLineAnchor) {
            return cglib::vec2<float>(0, 0);
        }
        return -calculateBoxPoint(*_style->calloutLineAnchor, scale, glyphScale);
    }

    cglib::vec2<float> Label::calculateBoxPoint(const cglib::vec2<float>& anchor, float scale, float glyphScale) const {
        if (_glyphBBox.min(0) > _glyphBBox.max(0)) {
            return cglib::vec2<float>(0, 0);
        }
        // The plates are part of the box the style points at: a leader line that stopped at the
        // glyph bounds would end inside them.
        cglib::bbox2<float> box = calculatePlatedBBox(_variantIndex, glyphScale);
        float x0 = box.min(0) * scale, x1 = box.max(0) * scale;
        float y0 = box.min(1) * scale, y1 = box.max(1) * scale;
        cglib::vec2<float> p(x0 + (x1 - x0) * (anchor(0) + 1.0f) * 0.5f, y0 + (y1 - y0) * (anchor(1) + 1.0f) * 0.5f);
        if (_style->transform) {
            p = cglib::transform(p, _style->transform->matrix2());
        }
        return p;
    }

    Label::LineLayout Label::buildLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, const ViewState& viewState, const cglib::mat4x4<double>& mvpMatrix, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const {
        const std::vector<Placement::Edge>& edges = placement->edges;
        if (edges.empty() || !(scale > 0)) {
            return LineLayout::NO_ROOM;
        }

        // Which plane the run is laid out in. 'billboard-line' projects the line onto the CAMERA
        // axes, so the text keeps its size and its shape at any tilt; 'line' lays it out on the
        // placement's own tangent frame, so the text lies flat on the map like the line it names.
        bool screenRun = isScreenLineRun();
        const cglib::vec3<float>& xBasis = (screenRun ? viewState.orientation[0] : placement->xAxis);
        const cglib::vec3<float>& yBasis = (screenRun ? viewState.orientation[1] : placement->yAxis);

        // A screen run is projected through the view-projection, not just onto the camera axes:
        // with a tilted view the far half of a line is compressed by the perspective divide, and
        // glyphs laid out on an orthographic projection of it drift off the line and pick up the
        // wrong angle. The basis is the anchor's own glyph unit, so the run stays in glyph units
        // either way. The matrix comes from the caller, which keys the cache on it (see
        // updateLineVertexData). A flat run is not projected at all - it IS the ground plane.
        cglib::vec3<double> anchorPos = placement->position;
        auto projectPoint = [&mvpMatrix, &viewState](const cglib::vec3<double>& pos, cglib::vec2<float>& result) {
            cglib::vec4<double> clipPos = cglib::transform(cglib::vec4<double>(pos(0), pos(1), pos(2), 1), mvpMatrix);
            if (!(clipPos(3) > 0)) {
                return false;
            }
            result = cglib::vec2<float>(static_cast<float>(clipPos(0) / clipPos(3) * viewState.aspect), static_cast<float>(clipPos(1) / clipPos(3)));
            return true;
        };

        // Where the screen's right and up are IN THE SPACE THE RUN IS LAID OUT IN - the identity for
        // a screen run, the camera axes projected onto the tangent frame for a flat one. Which way
        // the word reads is a screen question, and a flat run's own space cannot see the camera: its
        // x always points forward along the line, so a test there reads the word in vertex order and
        // half the lines come out upside down.
        cglib::vec2<float> screenX(1, 0), screenY(0, 1);
        if (!screenRun) {
            screenX = cglib::vec2<float>(cglib::dot_product(xBasis, viewState.orientation[0]), cglib::dot_product(yBasis, viewState.orientation[0]));
            screenY = cglib::vec2<float>(cglib::dot_product(xBasis, viewState.orientation[1]), cglib::dot_product(yBasis, viewState.orientation[1]));
        }

        cglib::vec2<float> anchorScreen, xUnitScreen, yUnitScreen;
        bool perspective = screenRun
            && projectPoint(anchorPos, anchorScreen)
            && projectPoint(anchorPos + cglib::vec3<double>::convert(xBasis * scale), xUnitScreen)
            && projectPoint(anchorPos + cglib::vec3<double>::convert(yBasis * scale), yUnitScreen);
        cglib::vec2<float> xUnit = xUnitScreen - anchorScreen;
        cglib::vec2<float> yUnit = yUnitScreen - anchorScreen;
        float determinant = xUnit(0) * yUnit(1) - xUnit(1) * yUnit(0);
        perspective = perspective && determinant != 0;

        float invScale = 1.0f / scale;
        auto toGlyphSpace = [&](const cglib::vec3<float>& offset, cglib::vec2<float>& result) {
            if (!perspective) {
                result = cglib::vec2<float>(cglib::dot_product(xBasis, offset) * invScale, cglib::dot_product(yBasis, offset) * invScale);
                return true;
            }
            cglib::vec2<float> screenPos;
            if (!projectPoint(anchorPos + cglib::vec3<double>::convert(offset), screenPos)) {
                return false;
            }
            cglib::vec2<float> delta = screenPos - anchorScreen;
            result = cglib::vec2<float>((delta(0) * yUnit(1) - delta(1) * yUnit(0)) / determinant,
                                        (xUnit(0) * delta(1) - xUnit(1) * delta(0)) / determinant);
            return true;
        };

        std::vector<cglib::vec2<float>> points;
        points.reserve(edges.size() + 1);
        for (const Placement::Edge& edge : edges) {
            cglib::vec2<float> point;
            if (!toGlyphSpace(edge.position0, point)) {
                break; // behind the camera: the line ends here as far as the glyphs are concerned
            }
            points.push_back(point);
        }
        if (points.size() == edges.size()) {
            cglib::vec2<float> point;
            if (toGlyphSpace(edges.back().position1, point)) {
                points.push_back(point);
            }
        }
        if (points.size() < 2) {
            return LineLayout::NO_ROOM;
        }

        // Arc length along the projected line, so a glyph can be put at a distance rather than
        // at a vertex: the pen advances by the glyph advances, not by whatever the line does.
        std::vector<float> lengths(points.size(), 0.0f);
        for (std::size_t i = 1; i < points.size(); i++) {
            lengths[i] = lengths[i - 1] + cglib::length(points[i] - points[i - 1]);
        }
        float total = lengths.back();
        if (!(total > 0)) {
            return LineLayout::NO_ROOM;
        }

        // Past either end the line is CONTINUED in its own direction rather than clamped: the ends
        // are where the tile that carries this copy of the feature was cut, not where the road
        // stops, and a run allowed to reach a little past them (see below) has to keep going
        // straight there instead of piling its last glyphs onto the end point.
        auto pointAt = [&points, &lengths](float distance) {
            if (distance <= 0) {
                cglib::vec2<float> edge = points[1] - points.front();
                return points.front() + (cglib::norm(edge) > 0 ? cglib::unit(edge) * distance : cglib::vec2<float>(0, 0));
            }
            if (distance >= lengths.back()) {
                cglib::vec2<float> edge = points.back() - points[points.size() - 2];
                return points.back() + (cglib::norm(edge) > 0 ? cglib::unit(edge) * (distance - lengths.back()) : cglib::vec2<float>(0, 0));
            }
            std::size_t i = 0;
            while (i + 2 < points.size() && lengths[i + 1] < distance) {
                i++;
            }
            float segmentLength = lengths[i + 1] - lengths[i];
            float t = (segmentLength > 0 ? (distance - lengths[i]) / segmentLength : 0.0f);
            return points[i] + (points[i + 1] - points[i]) * t;
        };

        std::size_t segment = std::min(placement->index, points.size() - 2);
        cglib::vec2<float> segmentVec = points[segment + 1] - points[segment];
        if (cglib::norm(segmentVec) == 0) {
            return LineLayout::NO_ROOM;
        }
        // The anchor is the origin of this space, so its distance along the line is where the pen
        // starts.
        float penStart = lengths[segment] + cglib::dot_product(-points[segment], cglib::unit(segmentVec));

        // Longest line of the run, in glyph units. The anchor is kept away from the ends of the
        // line in world units (clampPlacementAnchor), but the line the glyphs are actually laid
        // out on is the PROJECTED one: with a tilted view its far half is compressed by the
        // perspective divide, so a run that has room on the ground can still overrun the end.
        // Slide the run back onto the line rather than dropping the label - the smallest camera
        // move changes the compression, and dropping made labels blink in and out while panning.
        float runLength = 0;
        float lineLength = 0;
        for (const Font::Glyph& glyph : _glyphs) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                lineLength = 0;
                continue;
            }
            lineLength += glyph.advance(0);
            runLength = std::max(runLength, lineLength);
        }
        // The run may reach a little past either end, on the line's own direction: the projected
        // length of a line running away from a tilted camera changes with every camera step, and
        // an exact fit test on it means a label that blinks out whenever the compression takes a
        // fraction of a glyph more than the line has left. The allowance is the same room the
        // anchor is given on the ground (clampPlacementAnchor).
        float overhang = std::min(runLength, total) * static_cast<float>(PLACEMENT_ROOM_FACTOR - 1.0);
        if (runLength > total + 2 * overhang) {
            return LineLayout::NO_ROOM; // the line is genuinely too short to carry the text
        }
        penStart = std::min(std::max(penStart, -overhang), total - runLength + overhang);

        // WHICH WAY THE WORD READS, decided on the projected line over the span the glyphs cover -
        // tangram's CurvedLabel::updateScreenTransform. A segment past the tolerance forces its
        // direction, and needing both ways drops the label rather than drawing a mixture; otherwise
        // reverse when the end lands left of the start; and a hairpin (two segments in a short
        // window pointing back at each other, their |dir(k)+dir(i)|^2 < 1.7^2) is dropped, because
        // arc length grows around it while the screen position does not and the glyphs pile up.
        // The anchor's own tangent is not enough - on a curve it can point the other way.
        {
            float flipTolerance = std::sin(45.0f * 3.14159265f / 180.0f);
            bool mustForward = false, mustReverse = false;
            bool hairpin = false;
            std::size_t first = 0, last = points.size() - 2;
            while (first < last && lengths[first + 1] < penStart) {
                first++;
            }
            while (last > first && lengths[last] > penStart + runLength) {
                last--;
            }
            auto segmentDir = [&points](std::size_t i) {
                cglib::vec2<float> v = points[i + 1] - points[i];
                return (cglib::norm(v) > 0 ? cglib::unit(v) : cglib::vec2<float>(0, 0));
            };
            for (std::size_t i = first; i <= last; i++) {
                cglib::vec2<float> dir = segmentDir(i);
                if (dir == cglib::vec2<float>(0, 0)) {
                    continue;
                }
                float dirX = cglib::dot_product(dir, screenX);
                mustForward = mustForward || dirX > flipTolerance;
                mustReverse = mustReverse || dirX < -flipTolerance;
                for (std::size_t k = i; k-- > first; ) {
                    if (cglib::norm(segmentDir(k) + dir) < LINE_HAIRPIN_CHORD * LINE_HAIRPIN_CHORD) {
                        hairpin = true;
                        break;
                    }
                    if (lengths[k] < lengths[i] - LINE_DIRECTION_WINDOW) {
                        break;
                    }
                }
                if (hairpin) {
                    break;
                }
            }
            if (hairpin || (mustForward && mustReverse)) {
                return LineLayout::UNREADABLE; // the run doubles back on itself
            }
            // Which way the word reads is decided by the run's CHORD alone. Tangram lets a single
            // segment pointing the wrong way (their mustReverse) force the reversal, and on a line
            // that wiggles - a contour does constantly - one such segment turns a run that reads
            // perfectly well upside down. Their own comment there is "TODO use better heuristic to
            // decide flipping"; the segment test is kept, but only for the REJECTION above, where
            // it is unambiguous.
            cglib::vec2<float> chord = pointAt(penStart + runLength) - pointAt(penStart);
            float dx = cglib::dot_product(chord, screenX);
            float dy = cglib::dot_product(chord, screenY);
            // A flat run's chord is foreshortened by the tilt, so the thresholds are taken against
            // what it measures ON SCREEN rather than against its length on the ground.
            float chordRef = (screenRun ? runLength : cglib::length(cglib::vec2<float>(dx, dy)));
            if (std::abs(dx) < LINE_VERTICAL_RUN_FRACTION * chordRef) {
                // A run this close to vertical has no meaningful left or right, and a test on dx
                // alone picks one at random there. Map convention is that a vertical label reads
                // bottom to top, so the run must head UP the screen.
                _lineReversed = (dy < 0);
            }
            else {
                // Hysteresis, so a run whose chord is near the vertical band does not flip back and
                // forth on the smallest camera step.
                float bias = (_lineReversed ? LINE_REVERSE_HYSTERESIS : -LINE_REVERSE_HYSTERESIS) * chordRef;
                _lineReversed = (dx < bias);
            }
            if (_lineReversed) {
                std::reverse(points.begin(), points.end());
                for (std::size_t i = 0; i < points.size(); i++) {
                    lengths[i] = (i > 0 ? lengths[i - 1] + cglib::length(points[i] - points[i - 1]) : 0.0f);
                }
                penStart = total - (penStart + runLength);
            }
        }

        float offset = penStart;

        // Readability is judged against the direction the run takes as a whole, not against its
        // first glyph: measured from the first glyph, a gently curving line accumulates deviation
        // along the word and trips the test at whatever point the run happens to start - which the
        // perspective projection moves on every camera step, so the label blinks. The chord is
        // symmetric, so the same curve gives half the deviation and it does not depend on where
        // the run starts.
        cglib::vec2<float> runVec = pointAt(penStart + runLength) - pointAt(penStart);
        cglib::vec2<float> runDir = (cglib::norm(runVec) > 0 ? cglib::unit(runVec) : cglib::vec2<float>(0, 0));

        // Hysteresis: the deviation of a run laid out on the PROJECTED line moves with the camera -
        // over 3D terrain a tilted view re-compresses the line on every step - so a run sitting
        // near the threshold flips on the smallest pan, and the label blinks. A run that is
        // already laid out is given a wider allowance than one that is not yet placed, which
        // costs nothing in readability (the run has to be readable to get there in the first
        // place) and turns the flapping into a one-way transition.
        float minSegmentDotProduct = (_lineLayoutValid ? MIN_LINE_SEGMENT_DOTPRODUCT_KEEP : MIN_LINE_SEGMENT_DOTPRODUCT);
        float maxRunAngleSpread = (_lineLayoutValid ? MAX_LINE_RUN_ANGLE_SPREAD_KEEP : MAX_LINE_RUN_ANGLE_SPREAD);

        bool readable = true;
        cglib::vec2<float> prevDir(0, 0);
        float turnAngle = 0, minAngle = 0, maxAngle = 0;
        for (const Font::Glyph& glyph : _glyphs) {
            if (glyph.codePoint == Font::CR_CODEPOINT) {
                offset = penStart;
                prevDir = cglib::vec2<float>(0, 0);
                turnAngle = 0;
                continue;
            }

            float advance = glyph.advance(0);

            // Direction over the glyph's OWN span, not the direction of whatever tiny segment it
            // happens to start on: a line that shakes at a scale below the glyphs would otherwise
            // rotate every one of them on its own and tear the word apart.
            cglib::vec2<float> pen = pointAt(offset);
            cglib::vec2<float> next = pointAt(offset + std::max(advance, MIN_LINE_GLYPH_SPAN));
            cglib::vec2<float> spanVec = next - pen;
            if (cglib::norm(spanVec) == 0) {
                readable = false;
                break;
            }
            cglib::vec2<float> xAxis = cglib::unit(spanVec);
            cglib::vec2<float> yAxis(-xAxis(1), xAxis(0));
            if (runDir != cglib::vec2<float>(0, 0) && cglib::dot_product(xAxis, runDir) < minSegmentDotProduct) {
                readable = false;
            }
            if (prevDir != cglib::vec2<float>(0, 0)) {
                // A run whose chord says one thing while consecutive glyphs turn the other way is
                // torn apart even when every glyph stays near the chord.
                if (cglib::dot_product(xAxis, prevDir) < minSegmentDotProduct) {
                    readable = false;
                }
                // Total turn of the run, accumulated glyph by glyph rather than measured against
                // the chord: a run that follows a hairpin turns steadily - every glyph is close to
                // its neighbour, and the chord of the run is degenerate, so neither test above
                // sees anything - and the word then reads as a spiral.
                turnAngle += std::atan2(prevDir(0) * xAxis(1) - prevDir(1) * xAxis(0), cglib::dot_product(xAxis, prevDir));
                minAngle = std::min(minAngle, turnAngle);
                maxAngle = std::max(maxAngle, turnAngle);
            }
            prevDir = xAxis;

            if (glyph.codePoint != Font::SPACE_CODEPOINT) {
                cglib::vec2<float> base = pen + xAxis * glyph.offset(0) + yAxis * glyph.offset(1);
                cglib::vec2<float> p0 = base;
                cglib::vec2<float> p1 = base + xAxis * glyph.size(0);
                cglib::vec2<float> p2 = base + xAxis * glyph.size(0) + yAxis * glyph.size(1);
                cglib::vec2<float> p3 = base + yAxis * glyph.size(1);

                std::uint16_t i0 = static_cast<std::uint16_t>(vertices.size());
                indices.append(i0 + 0, i0 + 1, i0 + 2);
                indices.append(i0 + 0, i0 + 2, i0 + 3);

                std::int16_t u0 = static_cast<std::int16_t>(glyph.baseGlyph.x), u1 = static_cast<std::int16_t>(glyph.baseGlyph.x + glyph.baseGlyph.width);
                std::int16_t v0 = static_cast<std::int16_t>(glyph.baseGlyph.y), v1 = static_cast<std::int16_t>(glyph.baseGlyph.y + glyph.baseGlyph.height);
                texCoords.append(cglib::vec2<std::int16_t>(u0, v1), cglib::vec2<std::int16_t>(u1, v1), cglib::vec2<std::int16_t>(u1, v0), cglib::vec2<std::int16_t>(u0, v0));

                // attribs[0] carries the run the glyph belongs to until calculateVertexData turns
                // it into a style index: 1 = the second run, 2 = the icon run, both of which may
                // have their own colour.
                cglib::vec4<std::int8_t> attrib(glyph.icon ? 2 : (glyph.secondary ? 1 : 0), static_cast<std::int8_t>(glyph.baseGlyph.mode), 0, 0);
                attribs.append(attrib, attrib, attrib, attrib);

                vertices.append(cglib::vec3<float>(p0(0), p0(1), 0), cglib::vec3<float>(p1(0), p1(1), 0), cglib::vec3<float>(p2(0), p2(1), 0), cglib::vec3<float>(p3(0), p3(1), 0));
            }

            offset += advance;
        }
        if (maxAngle - minAngle > maxRunAngleSpread) {
            readable = false;
        }
        return readable ? LineLayout::PLACED : LineLayout::UNREADABLE;
    }

    void Label::updateLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, const ViewState& viewState, bool rebuildForView) const {
        // The run is laid out on the line AS THE CAMERA PROJECTS IT, so the view-projection is part
        // of the key: keyed on the camera axes alone a pan leaves the glyphs on a projection of the
        // road that no longer holds until a zoom rebuilds it.
        // Only the RENDERER rebuilds on a view change. The culler's view state lags the frame, and
        // its layout decides whether the label is drawn at all, so re-laying out there judges the
        // label against a view nobody sees; it reuses whatever layout exists.
        cglib::mat4x4<double> mvpMatrix = viewState.projectionMatrix * viewState.cameraMatrix;
        // A flat run does not follow the projection - it lies on the ground - but which way it
        // reads does, so the camera AXES are its key: a rotation turns the line under the word
        // and nothing else about the layout changes. That leaves a flat run reused across a pan,
        // where a screen run rebuilds.
        bool viewChanged = rebuildForView && (isScreenLineRun()
            ? mvpMatrix != _cachedMVPMatrix
            : viewState.orientation[0] != _cachedCameraXAxis || viewState.orientation[1] != _cachedCameraYAxis);
        if (scale == _cachedScale && placement == _cachedPlacement && !viewChanged) {
            return;
        }
        VT_STAT_INC(lineLayoutBuilds);
        _cachedVertices.clear();
        _cachedTexCoords.clear();
        _cachedAttribs.clear();
        _cachedIndices.clear();
        LineLayout layout = buildLineVertexData(placement, scale, viewState, mvpMatrix, _cachedVertices, _cachedTexCoords, _cachedAttribs, _cachedIndices);
        _cachedValid = (layout == LineLayout::PLACED);
        // A run that is already on screen rides out a few layouts that do not FIT. Fit is judged on
        // the projected line, and it is judged from two different view states - the culler works on
        // the snapshot of its pass, the renderer on the current camera - so a run at the edge of
        // what fits alternates between them, which shows up as a label blinking at frame rate.
        // An UNREADABLE run gets no such grace: the layout has just measured its glyphs turning far
        // enough to pile up on the inside of a corner, and holding that on screen is how a label
        // that the code already knows is illegible stays there for several passes.
        if (_cachedValid) {
            _lineLayoutFailures = 0;
        }
        else if (layout == LineLayout::NO_ROOM && _lineLayoutValid && ++_lineLayoutFailures <= LINE_LAYOUT_FAILURE_GRACE) {
            _cachedValid = true;
        }
        else {
            _lineLayoutFailures = 0;
            if (layout == LineLayout::UNREADABLE) {
                // Nothing of it is drawn while it fades out - the quads it just built are the
                // overlapping ones.
                _cachedVertices.clear();
                _cachedTexCoords.clear();
                _cachedAttribs.clear();
                _cachedIndices.clear();
            }
        }
        _lineLayoutValid = _cachedValid;
        _cachedScale = scale;
        _cachedPlacement = placement;
        _cachedMVPMatrix = mvpMatrix;
        _cachedCameraXAxis = viewState.orientation[0];
        _cachedCameraYAxis = viewState.orientation[1];
    }

    void Label::setupCoordinateSystem(const ViewState& viewState, const std::shared_ptr<const Placement>& placement, cglib::vec3<float>& origin, cglib::vec3<float>& xAxis, cglib::vec3<float>& yAxis) const {
        cglib::vec3<double> position = placement->position;
        if (viewState.planarProjection && !isLineRun() && viewState.resolution > 0) {
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
        case LabelOrientation::CALLOUT:
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

        // A LINE label decides this in buildLineVertexData instead, from the PROJECTED run's own
        // start and end - tangram's rule (CurvedLabel::updateScreenTransform). The anchor tangent
        // used below is only the direction at one point of the line: a curving line (a contour, a
        // bending street) can leave the anchor pointing right while the word runs left, and the
        // label then reads upside down. Flipping the placement here as well would fight that.
        if (isLineRun()) {
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
        // Keep the anchor on the segment it already sits on when its source line is still there.
        // Re-deriving scores by distance and only an anchor exactly on the line scores 0 - on
        // terrain it never does (a freshly decoded tile carries its decode-time heights), so the
        // mid-line weight decides instead and the anchor creeps toward the middle of the road on
        // every tile-set change. A copy clipped by a tile border can be a stub too short to carry
        // the text, so copies long enough beat the ones that are not, ahead of every other rule.
        double requiredLength = _placementTextLength * PLACEMENT_ROOM_FACTOR;
        auto isUsable = [&requiredLength](const TileLine& tileLine) {
            double length = 0;
            for (std::size_t i = 1; i < tileLine.vertices.size(); i++) {
                length += cglib::length(tileLine.vertices[i] - tileLine.vertices[i - 1]);
                if (length >= requiredLength) {
                    return true;
                }
            }
            return !(requiredLength > 0);
        };

        if (oldPlacement) {
            for (const TileLine& tileLine : tileLines) {
                if (!(tileLine.tileId == oldPlacement->tileId && tileLine.localId == oldPlacement->localId)) {
                    continue;
                }
                if (oldPlacement->sourceIndex + 1 < tileLine.vertices.size() && isUsable(tileLine)) {
                    return buildLinePlacement(tileLine, oldPlacement->sourceIndex, position);
                }
                break;
            }
        }

        const TileLine* bestTileLine = nullptr;
        std::size_t bestIndex = 0;
        cglib::vec3<double> bestPos = position;
        double bestDist = std::numeric_limits<double>::infinity();
        int bestRank = -1;
        for (const TileLine& tileLine : tileLines) {
            // A candidate from the placement's original source geometry always wins over
            // copies of the feature coming from other tiles (see snapPlacement).
            bool sameSource = oldPlacement && tileLine.tileId == oldPlacement->tileId && tileLine.localId == oldPlacement->localId;
            int rank = (isUsable(tileLine) ? 2 : 0) + (sameSource ? 1 : 0);
            if (rank < bestRank) {
                continue;
            }
            if (rank > bestRank) {
                bestTileLine = nullptr;
                bestIndex = 0;
                bestPos = position;
                bestDist = std::numeric_limits<double>::infinity();
                bestRank = rank;
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

        std::vector<cglib::vec3<double>> smoothedVertices;
        std::size_t smoothedIndex = bestIndex;
        smoothPlacementLine(bestTileLine->vertices, bestIndex, _placementTextLength * PLACEMENT_SMOOTH_TEXT_FRACTION, smoothedVertices, smoothedIndex);
        clampPlacementAnchor(smoothedVertices, _placementTextLength, smoothedIndex, bestPos);
        return std::make_shared<const Placement>(bestTileLine->tileId, bestTileLine->localId, smoothedVertices, smoothedIndex, bestIndex, bestPos, bestTileLine->normal);
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
        
        const TilePoint* bestTilePoint = nullptr;
        double bestDistance = std::numeric_limits<double>::infinity();
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
            // Take the candidate CLOSEST TO THE CAMERA rather than the first one that fits: the
            // anchors of a line are spread over everything loaded, so the first fitting one is
            // usually far away - the label then sits near the horizon, tiny, while the stretch of
            // line the user is looking at carries nothing.
            if (inside) {
                double distance = cglib::length(tilePoint.position - viewState.origin);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestTilePoint = &tilePoint;
                }
            }
        }
        if (bestTilePoint) {
            return std::make_shared<const Placement>(bestTilePoint->tileId, bestTilePoint->localId, bestTilePoint->position, bestTilePoint->normal, bestTilePoint->xAxis, bestTilePoint->yAxis);
        }
        return std::shared_ptr<const Placement>();
    }

    std::shared_ptr<const Label::Placement> Label::findClippedLinePlacement(const ViewState& viewState, const std::list<TileLine>& tileLines) const {
        // Clip each vertex list against frustum, if resulting list is inside frustum, return its center
        // How much line a candidate offers, in the units the run is laid out in (see below).
        double bestLen = 0;

        // World length of the glyph run BEFORE the terrain scale factor. Labels over planar 3D
        // terrain keep a constant on-screen size, so the world length a run needs depends on where
        // it is placed; _placementTextLength can not serve here, it carries the factor of the
        // placement this search is about to replace.
        double textLengthBase = 0;
        if (isLineRun()) {
            float glyphScale = (_style->sizeFunc)(viewState) * viewState.zoomScale * _style->scale;
            for (const Font::Glyph& glyph : _glyphs) {
                textLengthBase += glyph.advance(0) * glyphScale;
            }
        }

        // A candidate is measured in the plane its run will be laid out in, and both the ranking and
        // the fit test use that one measure.
        //  - a SCREEN run (billboard-line) is laid out on the projected line, so a line running away
        //    from a tilted camera is worth a fraction of its ground length and has to be compared on
        //    screen; measured on the ground it wins the placement and the run then does not fit.
        //  - a FLAT run (line) lies on the ground, so its own length is the ground length. Comparing
        //    it against a screen-HORIZONTAL run of the same world length asks for several times the
        //    room it needs as soon as the view tilts, and the label only appears once the camera is
        //    close enough for that inflated demand - which is what "have to zoom in a lot" was.
        bool screenRun = isScreenLineRun();
        cglib::mat4x4<double> mvpMatrix = viewState.projectionMatrix * viewState.cameraMatrix;
        auto projectPoint = [&mvpMatrix, &viewState](const cglib::vec3<double>& pos, cglib::vec2<double>& result) {
            cglib::vec4<double> clipPos = cglib::transform(cglib::vec4<double>(pos(0), pos(1), pos(2), 1), mvpMatrix);
            if (!(clipPos(3) > 0)) {
                return false;
            }
            result = cglib::vec2<double>(clipPos(0) / clipPos(3) * viewState.aspect, clipPos(1) / clipPos(3));
            return true;
        };
        // How much line the run needs at the given position, in the same units a candidate is
        // measured in: the ground length it covers, or that length laid across the view.
        auto requiredLength = [&](const cglib::vec3<double>& pos) {
            double worldLength = textLengthBase * calculateTerrainScaleFactor(pos, viewState);
            if (!screenRun) {
                return worldLength;
            }
            cglib::vec3<double> runVec = cglib::vec3<double>::convert(viewState.orientation[0]) * worldLength;
            cglib::vec2<double> p0, p1;
            if (!projectPoint(pos, p0) || !projectPoint(pos + runVec, p1)) {
                return std::numeric_limits<double>::infinity();
            }
            return cglib::length(p1 - p0);
        };
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
                double screenLen = 0;
                bool projectable = true;
                cglib::vec3<double> midPos = tileLine.vertices[(t0.first + t1.first) / 2];
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

                    cglib::vec2<double> screenPos0, screenPos1;
                    if (!projectPoint(pos0, screenPos0) || !projectPoint(pos1, screenPos1)) {
                        projectable = false; // partly behind the camera: the glyphs can not use it
                        break;
                    }
                    screenLen += cglib::length(screenPos1 - screenPos0);
                }

                double candidateLen = (screenRun ? screenLen : len);
                if (projectable && candidateLen > bestLen && candidateLen >= requiredLength(midPos) * PLACEMENT_ROOM_FACTOR) {
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
                            std::vector<cglib::vec3<double>> smoothedVertices;
                            std::size_t smoothedIndex = i;
                            smoothPlacementLine(tileLine.vertices, i, _placementTextLength * PLACEMENT_SMOOTH_TEXT_FRACTION, smoothedVertices, smoothedIndex);
                            clampPlacementAnchor(smoothedVertices, _placementTextLength, smoothedIndex, pos);
                            bestPlacement = std::make_shared<const Placement>(tileLine.tileId, tileLine.localId, smoothedVertices, smoothedIndex, i, pos, tileLine.normal);
                            bestLen = candidateLen;
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
