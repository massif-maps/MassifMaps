/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_LABEL_H_
#define _CARTO_VT_LABEL_H_

#include "TileId.h"
#include "TileLabel.h"

#include "TileTransformer.h"
#include "Color.h"
#include "Bitmap.h"
#include "Font.h"
#include "ViewState.h"
#include "VertexArray.h"
#include "Styles.h"

#include <cstdint>
#include <atomic>
#include <memory>
#include <array>
#include <list>
#include <vector>
#include <limits>
#include <algorithm>

namespace carto::vt {
    // Where a label's plate colours landed in the draw batch (see TileLabel::Style::Plate). -1 is
    // 'this one is not drawn'.
    struct LabelPlateIndices {
        int textFill = -1;
        int textBorder = -1;
        int iconFill = -1;
        int iconBorder = -1;
    };

    class Label final {
    public:
        explicit Label(const TileLabel& tileLabel, const TileId& tileId, int layerIdx, const cglib::mat4x4<double>& tileMatrix, const std::shared_ptr<const TileTransformer::VertexTransformer>& transformer);

        long long getGlobalId() const { return _globalId; }
        long long getLocalId() const { return _placement ? _placement->localId : _localId; }
        int getGeoPointIndex() const { return _geoPointIndex; }
        long long getGroupId() const { return _groupId; }

        TileId getTileId() const { return _placement ? _placement->tileId : _tileId; }
        int getLayerIndex() const { return _layerIndex; }

        const std::shared_ptr<const TileLabel::Style>& getStyle() const { return _style; }

        cglib::vec3<float> getNormal() const { return _placement ? _placement->normal : cglib::vec3<float>(0, 0, 0); }
        float getPriority() const { return _priority; }
        float getMinimumGroupDistance() const { return _minimumGroupDistance; }
        bool allowOverlapSameFeatureId() const { return _allowOverlapSameFeatureId; }
        bool sameFeatureIdDependent() const { return _sameFeatureIdDependent; }

        bool isValid() const { return (bool) _placement; }

        float getOpacity() const { return _opacity; }
        // A fade in progress only moves one byte per glyph, which the renderer patches into the
        // kept batch; a label appearing or disappearing changes what the batch HOLDS.
        void setOpacity(float opacity) {
            if (_opacity == opacity) {
                return;
            }
            bool structural = (_opacity <= 0.0f || opacity <= 0.0f);
            _opacity = opacity;
            if (structural) {
                touchDrawGeneration();
            } else if (isDrawGenerationTracked()) {
                _opacityGeneration.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Bumped by everything the DRAWN vertex data depends on - placement, layout, opacity. The
        // renderer keeps its 3D label batches across frames and this is what makes them stale
        // (docs/rendering/06-labels.md); it is global, so any label invalidates every batch.
        static std::uint64_t getDrawGeneration() { return _drawGeneration.load(std::memory_order_relaxed); }
        // Fades only, and only of labels already in the batch: the renderer patches those in place.
        static std::uint64_t getOpacityGeneration() { return _opacityGeneration.load(std::memory_order_relaxed); }

        bool isVisible() const { return _visible; }
        void setVisible(bool visible) { _visible = visible; }

        bool isActive() const { return _active; }
        void setActive(bool active) { _active = active; }

        // Which of the style's sides the text is laid out on (see TileLabel::Variant). Owned by
        // LabelCuller, the only place that knows what else is on screen; carried over by
        // snapPlacement, or a label rebuilt on a tile-set change would take the first side again
        // and the name would hop from one side of its icon to the other while the map pans.
        std::size_t getVariantCount() const { return _variants.size(); }
        int getVariantIndex() const { return _variantIndex; }
        void setVariantIndex(int index);
        bool drawsText() const { return _variants.empty() || _variants[_variantIndex].drawText; }

        // CALLOUT orientation: how far the label is lifted from its anchor, in SCREEN PIXELS along
        // the camera up axis. Owned by LabelCuller, which is the only place that knows what else is
        // on screen; the envelope and the vertex data both read it, so the leader line always ends
        // where the glyphs actually are.
        float getCalloutOffset() const { return _calloutOffset; }
        void setCalloutOffset(float offset) { _calloutOffset = offset; }

        // The screen line the culler put this callout on, and where its anchor was when it did.
        // The anchor MOVES between placement passes - elevation tiles stream in and re-anchor the
        // label on the GL thread, a tilt slides it up or down the screen - and a lift measured
        // against the old anchor takes the label off the row. Keeping the anchor's screen position
        // lets the draw path correct for exactly that, so the label stays on its line.
        void setCalloutPlacement(float offset, float anchorScreenY) { _calloutOffset = offset; _calloutAnchorScreenY = anchorScreenY; _calloutAnchored = true; }
        float calculateAnchorScreenY(const ViewState& viewState) const;

        // Placement passes this callout has failed in a row while it was on screen. The style may
        // allow a few (TileLabel::Style::calloutPersistPasses): a panning map rebuilds its label
        // set constantly, and a name that loses its row for one pass and takes it again on the
        // next reads as a flicker.
        int getCalloutFailures() const { return _calloutFailures; }
        void setCalloutFailures(int failures) { _calloutFailures = failures; }

        // Identifies the set of tile geometries this label was built from, so that a rebuild
        // can tell whether anything about its source actually changed (see
        // GLTileRenderer::buildLabelMaps). Order-independent: the merge order follows the
        // visible tile order, which is not stable.
        void setGeometrySignature(long long hash, int count) { _geometryHash = hash; _geometryCount = count; }
        bool hasGeometrySignature(long long hash, int count) const { return _geometryCount == count && _geometryHash == hash && count > 0; }

        // Terrain re-anchoring state (see updateElevation). A label is anchored once, when it
        // is built, and re-anchored only when the elevation under one of the tiles it is built
        // from actually changes. Re-anchoring costs one elevation sample per line vertex, so
        // doing it for every label on every tile-set change is a whole-screen resample.
        // A label that has ALREADY been anchored and is neither placed nor on screen defers its
        // re-anchor instead of resampling every vertex for something nothing draws: it keeps the
        // heights it has (one LOD step out at worst, because the tile that just arrived replaces
        // an ancestor it was already sampling) and reports itself dirty again as soon as the
        // culler gives it a placement. A label that has NEVER been anchored never defers - its
        // geometry is still flat, and placing it at sea level under a mountain is what makes
        // labels pop. Measured before this: ~750 000 elevation samples a frame, most of them for
        // labels with no placement.
        bool isElevationDirty() const { return _elevationDirty && (!_elevationAnchored || _visible || _opacity > 0.0f || (bool) _placement); }
        void setElevationDirty(bool dirty) { _elevationDirty = dirty; }
        bool hasGeometryOverTile(const TileId& tileId) const;

        void mergeGeometries(Label& label);
        void snapPlacement(const Label& label);
        bool updatePlacement(const ViewState& viewState);
        void updateElevation(const std::function<double(const cglib::vec3<double>&)>& heightFunc);

        // Which part of a label a draw pass wants. CALLOUT leader lines are drawn in a pass of
        // their own, BEFORE all text, so that no label's line crosses another label's glyphs.
        enum class DrawPass { ALL, CALLOUT_LINE, TEXT };

        bool calculateCenter(cglib::vec3<double>& pos) const;
        bool calculateEnvelope(const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const { return calculateEnvelope((_style->sizeFunc)(viewState), 0, viewState, envelope); }
        bool calculateEnvelope(float size, float buffer, const ViewState& viewState, std::array<cglib::vec3<float>, 4>& envelope) const;
        // The envelope of EVERY side the text may be laid out on, in one call. The placement, the
        // scale and the label's screen axes are the same for all of them - only the glyph box moves
        // - so the culler, which tries the sides in order, pays for one placement instead of one
        // per side. Falls back to the single current envelope for a label with no variants.
        bool calculateVariantEnvelopes(float size, float buffer, const ViewState& viewState, std::vector<std::array<cglib::vec3<float>, 4>>& envelopes) const;
        bool calculateVertexData(const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices, DrawPass pass = DrawPass::ALL, const LabelPlateIndices& plates = LabelPlateIndices(), int secondaryStyleIndex = -1, int iconStyleIndex = -1) const { return calculateVertexData((_style->sizeFunc)(viewState), viewState, styleIndex, haloStyleIndex, vertices, offsets, normals, texCoords, attribs, indices, pass, plates, secondaryStyleIndex, iconStyleIndex); }
        bool calculateVertexData(float size, const ViewState& viewState, int styleIndex, int haloStyleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices, DrawPass pass = DrawPass::ALL, const LabelPlateIndices& plates = LabelPlateIndices(), int secondaryStyleIndex = -1, int iconStyleIndex = -1) const;

    private:
        // How labelVsh must read a glyph offset (attribs[3]); see calculateVertexData.
        static constexpr std::int8_t WORLD_OFFSET = 0;       // already spanned, add it as is
        static constexpr std::int8_t CAMERA_AXIS_OFFSET = 1; // x/y on the camera axes
        // Same, plus the perspective cancel the CPU used to bake into the offset (see labelVsh).
        // The offset is then free of view depth, which is what a persistent batch needs.
        static constexpr std::int8_t CAMERA_AXIS_DEPTH_OFFSET = 2;

        static constexpr unsigned int MAX_LABEL_VERTICES = 16384;
        static constexpr unsigned int MAX_LINE_FITTING_ITERATIONS = 1; // number of iterations for line glyph placement on corners

        static constexpr float SUMMED_ANGLE_SPLIT_THRESHOLD = 2.09f; // maximum sum of segment angles, in radians
        static constexpr float SINGLE_ANGLE_SPLIT_THRESHOLD = 1.57f; // maximum single segment angle, in radians
        static constexpr float MIN_LINE_SEGMENT_DOTPRODUCT = 0.5f; // the minimum allowed dot product between consecutive segments
        static constexpr float MIN_LINE_SEGMENT_DOTPRODUCT_KEEP = 0.3f; // the same, for a run that is already laid out (hysteresis)
        static constexpr float MAX_LINE_RUN_ANGLE_SPREAD = 1.22f; // total turn a glyph run may take, in radians (~70 deg)
        static constexpr float MAX_LINE_RUN_ANGLE_SPREAD_KEEP = 1.57f; // the same, for a run that is already laid out (hysteresis)
        static constexpr int LINE_LAYOUT_FAILURE_GRACE = 4; // layouts a run that is already on screen may fail before it is dropped
        static constexpr float MIN_LINE_GLYPH_SPAN = 0.5f; // shortest span a glyph takes its direction from, in glyph units
        // Tangram's hairpin test (CurvedLabel::updateScreenTransform): two segments within a short
        // window whose directions sum to less than this chord point back at each other - an inner
        // angle under ~120 degrees - and the glyphs pile up on each other there. Their window is 20
        // screen pixels; in glyph units (1 unit = the font size) that is a little over one glyph.
        static constexpr float LINE_HAIRPIN_CHORD = 1.7f;
        static constexpr float LINE_DIRECTION_WINDOW = 1.5f; // glyph units
        static constexpr float LINE_REVERSE_HYSTERESIS = 0.02f; // fraction of the run length
        static constexpr float LINE_VERTICAL_RUN_FRACTION = 0.2f; // |dx| below this fraction of the run counts as vertical
        static constexpr double PLACEMENT_ROOM_FACTOR = 1.25; // room the glyph run is given on the line, relative to its own length
        static constexpr double PLACEMENT_SMOOTH_TEXT_FRACTION = 1.0 / 3.0; // line detail below this fraction of the text length is smoothed away before laying out glyphs
        static constexpr double SNAP_MOVE_EPSILON = 1.0e-9; // internal world units (1 unit ~ 38m); a 1px anchor drift is ~1e-4 at z15
        static constexpr float MIN_BILLBOARD_VIEW_NORMAL_DOTPRODUCT = 0.1f; // the minimum allowed dot product between view vector and surface normal (cos ~78.5deg -> labels valid down to ~tilt 11.5; was 0.49 = calibrated to the old 30deg tilt clamp)

        struct TilePoint {
            TileId tileId;
            long long localId;
            cglib::vec3<double> position;
            cglib::vec3<float> normal;
            cglib::vec3<float> xAxis;
            cglib::vec3<float> yAxis;

            explicit TilePoint(const TileId& tileId, long long localId, const cglib::vec3<double>& pos, const cglib::vec3<float>& norm, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis) : tileId(tileId), localId(localId), position(pos), normal(norm), xAxis(xAxis), yAxis(yAxis) { }

            bool operator == (const TilePoint& other) const {
                return tileId == other.tileId && localId == other.localId;
            }

            bool operator != (const TilePoint& other) const {
                return !(*this == other);
            }
        };

        struct TileLine {
            TileId tileId;
            long long localId;
            std::vector<cglib::vec3<double>> vertices;
            cglib::vec3<float> normal;

            explicit TileLine(const TileId& tileId, long long localId, std::vector<cglib::vec3<double>> vertices, const cglib::vec3<float>& norm) : tileId(tileId), localId(localId), vertices(std::move(vertices)), normal(norm) { }

            bool operator == (const TileLine& other) const {
                return tileId == other.tileId && localId == other.localId;
            }

            bool operator != (const TileLine& other) const {
                return !(*this == other);
            }
        };

        struct Placement {
            struct Edge {
                cglib::vec3<float> position0;
                cglib::vec3<float> position1;
                cglib::vec3<float> binormal0;
                cglib::vec3<float> binormal1;
                cglib::vec3<float> xAxis;
                cglib::vec3<float> yAxis;
            };
            
            TileId tileId;
            long long localId;
            std::vector<Edge> edges;
            std::size_t index;
            std::size_t sourceIndex; // index of the anchor segment in the SOURCE line, edges may be a smoothed copy of it
            cglib::vec3<double> position;
            cglib::vec3<float> normal;
            cglib::vec3<float> xAxis;
            cglib::vec3<float> yAxis;

            explicit Placement(const TileId& tileId, long long localId, const cglib::vec3<double>& pos, const cglib::vec3<float>& norm, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis) : tileId(tileId), localId(localId), edges(), index(0), sourceIndex(0), position(pos), normal(norm), xAxis(xAxis), yAxis(yAxis) { }

            explicit Placement(const TileId& tileId, long long localId, const std::vector<cglib::vec3<double>>& vertices, std::size_t index, std::size_t sourceIndex, const cglib::vec3<double>& pos, const cglib::vec3<float>& norm) : tileId(tileId), localId(localId), edges(), index(index), sourceIndex(sourceIndex), position(pos), normal(norm), xAxis(0, 0, 0), yAxis(0, 0, 0) {
                if (vertices.size() > 1) {
                    edges.resize(vertices.size() - 1);
                    for (std::size_t i = 0; i < edges.size(); i++) {
                        Edge& edge = edges[i];
                        edge.position0 = cglib::vec3<float>::convert(vertices[i] - pos);
                        edge.position1 = cglib::vec3<float>::convert(vertices[i + 1] - pos);
                        edge.xAxis = cglib::unit(edge.position1 - edge.position0);
                        edge.yAxis = cglib::unit(cglib::vector_product(norm, edge.xAxis));
                        edge.binormal0 = edge.yAxis;
                        edge.binormal1 = edge.yAxis;
                        if (i > 0) {
                            cglib::vec3<float> binormal = edges[i - 1].yAxis + edges[i].yAxis;
                            if (cglib::norm(binormal) != 0) {
                                binormal = cglib::unit(binormal);
                                edges[i - 1].binormal1 = edges[i].binormal0 = binormal * (1.0f / cglib::dot_product(edges[i - 1].yAxis, binormal));
                            }
                        }
                    }
                    xAxis = edges[index].xAxis;
                    yAxis = edges[index].yAxis;
                }
            }

            void reverse() {
                if (!edges.empty()) {
                    index = edges.size() - 1 - index;
                    std::reverse(edges.begin(), edges.end());
                    for (Edge& edge : edges) {
                        std::swap(edge.position0, edge.position1);
                        std::swap(edge.binormal0, edge.binormal1);
                        edge.binormal0 = -edge.binormal0;
                        edge.binormal1 = -edge.binormal1;
                        edge.xAxis = -edge.xAxis;
                        edge.yAxis = -edge.yAxis;
                    }
                }
                xAxis = -xAxis;
                yAxis = -yAxis;
            }
        };
        
        // A point OF THE LABEL BOX from a normalized anchor - (-1,-1) the bottom left corner of the
        // text (the plate's padding included), (0,0) the centre, (1,1) the top right - in drawn
        // offset units, rotated with the glyphs. Both the leader line's end and the row the culler
        // aligns the label on are one of these, so a rotated name can hang from its first letter.
        // 'glyphScale' is glyph units per SCREEN PIXEL (1 / the label size): what the plates add
        // around the text is a pixel amount, and the box it grows is in glyph units.
        cglib::vec2<float> calculateBoxPoint(const cglib::vec2<float>& anchor, float scale, float glyphScale) const;
        // Whether the surface the label is anchored on is seen steeply enough for the label to be
        // worth drawing. Always true for a CALLOUT, which is a screen object (see the definition).
        bool isSurfaceFacingView(const ViewState& viewState, const Placement& placement) const;
        // How far the label is moved so that the style's line anchor lands on its feature's
        // vertical; zero unless the style names one.
        cglib::vec2<float> calculateCalloutShift(float scale, float glyphScale) const;
        // World units one SCREEN PIXEL is worth at the label's own depth, read off the projection
        // instead of the label's scale: the scale comes from the zoom, so converting with it makes
        // a callout's lift drift up and down the screen whenever the camera moves.
        float calculatePixelToWorld(const ViewState& viewState, const Placement& placement, float fallback) const;
        // World units one glyph unit is worth. Zoom-derived for an ordinary label (that is what
        // keeps it the same size as the rest of the map); taken off the projection for a CALLOUT,
        // which is a screen object and has to keep its pixel size whatever the camera does.
        float calculateLabelScale(float size, const ViewState& viewState, const std::shared_ptr<const Placement>& placement) const;
        // The lift to draw with: what the culler chose, corrected for how far the anchor has moved
        // on screen since (see setCalloutPlacement).
        float calculateCalloutLift(const ViewState& viewState) const;
        float calculateTerrainScaleFactor(const Placement& placement, const ViewState& viewState) const;
        float calculateTerrainScaleFactor(const cglib::vec3<double>& position, const ViewState& viewState) const;
        // Only a 3D label counts: the batches kept across frames are the 3D pass's, and 2D road
        // labels re-place and fade constantly.
        bool isDrawGenerationTracked() const {
            return _style->orientation == LabelOrientation::BILLBOARD_3D || _style->orientation == LabelOrientation::LINE_BILLBOARD_3D;
        }
        void touchDrawGeneration() const {
            if (isDrawGenerationTracked()) {
                _drawGeneration.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Most placement churn is on labels nothing draws - measured over one city pan, 4184
        // re-anchors of unplaced labels against 24 of visible ones - and those leave a kept batch
        // alone, because the batch only ever held the drawn ones.
        void touchDrawGenerationIfDrawn() const { if (_opacity > 0.0f) { touchDrawGeneration(); } }
        // snapAnchor false leaves the anchor unsnapped, for the modes where labelVsh snaps instead.
        void setupCoordinateSystem(const ViewState& viewState, const std::shared_ptr<const Placement>& placement, cglib::vec3<float>& origin, cglib::vec3<float>& xAxis, cglib::vec3<float>& yAxis, bool snapAnchor = true) const;
        void buildPointVertexData(VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;
        // Appends the plates behind the text and behind the icon - each one three quads, so the
        // corners keep their radius at any text width, and each one drawn with its own style index
        // (its own colour) before the glyphs. A border is one more plate behind the fill, grown by
        // the border width.
        void appendLabelPlates(float size, float scale, const std::shared_ptr<const Placement>& placement, const LabelPlateIndices& plates, const cglib::vec2<float>& calloutShift, const cglib::vec3<float>& origin, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;
        // One plate: the 3-sliced rounded rectangle around 'box', grown by 'grow' glyph units.
        void appendPlate(const cglib::bbox2<float>& box, const GlyphMap::Glyph& glyph, float radius, const cglib::vec2<float>& grow, float scale, int styleIndex, bool cameraAxes, const cglib::vec2<float>& calloutShift, const cglib::vec3<float>& origin, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis, const std::shared_ptr<const Placement>& placement, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;
        // Appends the leader line to a draw batch (nothing for a label that has none).
        void appendCalloutLine(float size, float scale, const ViewState& viewState, const std::shared_ptr<const Placement>& placement, int styleIndex, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec3<float>>& offsets, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;
        // The leader line quad, in the same units as the drawn glyph offsets. Built per frame
        // rather than cached with the text: its length is the culler's offset, which changes with
        // everything else on screen.
        void buildCalloutLineVertexData(float calloutLift, float pixelScale, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;
        void updateLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, const ViewState& viewState, bool rebuildForView) const;
        bool buildLineVertexData(const std::shared_ptr<const Placement>& placement, float scale, const ViewState& viewState, const cglib::mat4x4<double>& mvpMatrix, VertexArray<cglib::vec3<float>>& vertices, VertexArray<cglib::vec2<std::int16_t>>& texCoords, VertexArray<cglib::vec4<std::int8_t>>& attribs, VertexArray<std::uint16_t>& indices) const;

        // Where the text pen starts for the variant in use - zero for a label with one fixed
        // layout, which is every label a style without anchors builds.
        cglib::vec2<float> calculateVariantShift() const { return _variants.empty() ? cglib::vec2<float>(0, 0) : _variants[_variantIndex].shift; }
        // Which glyphs of the run a box covers: the icon prefix, the text after the first line
        // break, or both.
        enum class Part { ALL, ICON, TEXT };
        // The one pen walk over the glyph run, shared by the box and the quads so the two can not
        // drift apart: 'fn(glyph, pen, isText)' is called for every glyph that is drawn. The icon
        // glyphs come before the first line break, the text after it starts at 'shift' (plus its
        // line's justification), and !drawText stops at the break.
        template <typename Func>
        void walkGlyphs(const cglib::vec2<float>& shift, bool drawText, float lineAlign, Func fn) const {
            cglib::vec2<float> pen(0, 0);
            bool text = false;
            std::size_t lineIndex = 0;
            for (const Font::Glyph& glyph : _glyphs) {
                if (glyph.codePoint == Font::CR_CODEPOINT) {
                    if (!drawText) {
                        return;
                    }
                    pen = shift + cglib::vec2<float>(calculateLineShift(text ? ++lineIndex : lineIndex, lineAlign), 0);
                    text = true;
                }
                else {
                    fn(glyph, pen, text);
                }
                pen += glyph.advance;
            }
        }
        cglib::bbox2<float> calculateGlyphBBox(const cglib::vec2<float>& shift, bool drawText, Part part = Part::ALL, float lineAlign = 0.0f) const;
        // Justification of the text's lines for the variant in use: -1 flush left, 0 centred (which
        // is how the formatter laid them out), +1 flush right.
        float calculateLineAlign() const { return _variants.empty() ? _style->textLineAlign : _variants[_variantIndex].lineAlign; }
        // How far a line moves for that justification. The formatter centres every line inside the
        // block, so this is the distance from the centred position to the flush one; it is 0 for a
        // single-line label, which is nearly all of them.
        float calculateLineShift(std::size_t lineIndex, float lineAlign) const;
        // Ink extents of every line of the text, in the pen's own frame, plus the block's - the two
        // things a justification needs. Measured once, when the label is built.
        void measureTextLines();
        // The variant's content box grown by whatever its plates add around it - what the label
        // actually covers on screen, which is what the culler has to test.
        cglib::bbox2<float> calculatePlatedBBox(int variantIndex, float glyphScale) const;
        // The four corners a glyph box takes on the label's screen axes, style transform included.
        void buildBoxEnvelope(const cglib::bbox2<float>& glyphBBox, float scale, const cglib::vec2<float>& padding, const cglib::vec3<float>& origin, const cglib::vec3<float>& xAxis, const cglib::vec3<float>& yAxis, std::array<cglib::vec3<float>, 4>& envelope) const;

        cglib::bbox3<double> calculateGeometryBBox(const ViewState& viewState) const;
        static void smoothPlacementLine(const std::vector<cglib::vec3<double>>& vertices, std::size_t index, double minEdgeLength, std::vector<cglib::vec3<double>>& smoothedVertices, std::size_t& smoothedIndex);
        static void clampPlacementAnchor(const std::vector<cglib::vec3<double>>& vertices, double textLength, std::size_t& index, cglib::vec3<double>& position);
        std::shared_ptr<const Placement> buildLinePlacement(const TileLine& tileLine, std::size_t index, const cglib::vec3<double>& position) const;
        std::shared_ptr<const Placement> getPlacement(const ViewState& viewState) const;
        std::shared_ptr<const Placement> findSnappedPointPlacement(const cglib::vec3<double>& position, const std::list<TilePoint>& tilePoints, const Placement* oldPlacement = nullptr) const;
        std::shared_ptr<const Placement> findSnappedLinePlacement(const cglib::vec3<double>& position, const std::list<TileLine>& tileLines, const Placement* oldPlacement = nullptr) const;
        std::shared_ptr<const Placement> findClippedPointPlacement(const ViewState& viewState, const std::list<TilePoint>& tilePoints) const;
        std::shared_ptr<const Placement> findClippedLinePlacement(const ViewState& viewState, const std::list<TileLine>& tileLines) const;

        const TileId _tileId;
        const int _layerIndex;
        const int _geoPointIndex;
        const long long _localId;
        const long long _globalId;
        const long long _groupId;
        const std::vector<Font::Glyph> _glyphs;
        const std::vector<TileLabel::Variant> _variants;
        const std::shared_ptr<const TileLabel::Style> _style;
        const float _priority;
        const float _minimumGroupDistance;
        const bool _allowOverlapSameFeatureId;
        const bool _sameFeatureIdDependent;

        cglib::bbox2<float> _glyphBBox;               // of the variant in use
        cglib::bbox2<float> _textBBox;                // its text part alone (what the text plate sits behind)
        cglib::bbox2<float> _iconBBox;                // the icon run, which no variant moves
        std::vector<cglib::vec2<float>> _lineExtents; // per line of the text: ink min/max x, pen-relative
        cglib::vec2<float> _blockExtent = cglib::vec2<float>(0, 0); // the same over all of them
        std::vector<cglib::bbox2<float>> _variantBBoxes;
        std::vector<cglib::bbox2<float>> _variantTextBBoxes;
        int _variantIndex = 0;
        float _maxGlyphExtent = 0; // largest distance a glyph reaches from the anchor, style transform and every variant included
        std::list<TilePoint> _tilePoints;
        std::list<TileLine> _tileLines;

        mutable bool _geometryBBoxValid = false;
        mutable cglib::bbox3<double> _geometryBBox = cglib::bbox3<double>::smallest();

        // Length of the glyph run in world units at the view the placement was made for: the line
        // detail worth following and the room the run needs both derive from it. Refreshed by
        // updatePlacement, carried over by snapPlacement so a re-created label places the same way.
        double _placementTextLength = 0;

        float _calloutOffset = 0.0f; // screen pixels along the camera up axis, CALLOUT only (see setCalloutOffset)
        float _calloutAnchorScreenY = 0.0f;
        bool _calloutAnchored = false;
        int _calloutFailures = 0;
        float _opacity = 0.0f;
        bool _visible = false;
        bool _active = false;
        bool _elevationDirty = true;     // built flat: anchor it onto the terrain on the next frame
        bool _elevationAnchored = false; // has been anchored at least once, so a re-anchor may wait
        long long _geometryHash = 0;
        int _geometryCount = 0;

        std::shared_ptr<const Placement> _placement;
        mutable std::shared_ptr<const Placement> _cachedFlippedPlacement;

        static inline std::atomic<std::uint64_t> _drawGeneration{0};
        static inline std::atomic<std::uint64_t> _opacityGeneration{0};

        // Verdict of the last line layout, kept across cache rebuilds (and carried over by
        // snapPlacement) so the readability test below can be hysteretic.
        mutable bool _lineLayoutValid = false;
        mutable bool _lineReversed = false; // the projected run reads right to left, so the glyphs walk the line backwards
        mutable int _lineLayoutFailures = 0;

        mutable bool _cachedValid = false;
        mutable float _cachedScale = 0;
        // The view-projection the cached run was laid out for. The run follows the line as the
        // camera PROJECTS it (see buildLineVertexData), so the whole matrix is the key - a
        // camera that only moved leaves the axes and the scale alone while the perspective
        // compression along the line changes, and the glyphs then walk a road the camera no
        // longer sees that way. Tangram rebuilds its screen transform every frame for the same
        // reason (LabelManager::updateLabelSet -> CurvedLabel::updateScreenTransform); this
        // keeps the frame-to-frame reuse only for a camera that has not moved at all.
        mutable cglib::mat4x4<double> _cachedMVPMatrix = cglib::mat4x4<double>::zero();
        mutable std::shared_ptr<const Placement> _cachedPlacement;
        mutable VertexArray<cglib::vec3<float>> _cachedVertices;
        mutable VertexArray<cglib::vec2<std::int16_t>> _cachedTexCoords;
        mutable VertexArray<cglib::vec4<std::int8_t>> _cachedAttribs;
        mutable VertexArray<std::uint16_t> _cachedIndices;
    };
}

#endif
