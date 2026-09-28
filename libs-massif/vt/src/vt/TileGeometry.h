/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_TILEGEOMETRY_H_
#define _MASSIF_VT_TILEGEOMETRY_H_

#include "Bitmap.h"
#include "Color.h"
#include "StrokeMap.h"
#include "VertexArray.h"
#include "Styles.h"
#include "ExtrusionFloor.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <array>
#include <vector>
#include <algorithm>

#include <cglib/mat.h>

namespace massif::vt {
    class ExtrusionOccluder;

    // Hash of a style parameter's current value, so a selection change is a byte rewrite, not a tile
    // decode. Written by the app thread, read by the render thread.
    using StyleStateRef = std::shared_ptr<const std::atomic<std::uint64_t>>;

    class TileGeometry final {
    public:
        enum class Type {
            // POLYGON3DGROUND: an extrusion's contact shadow, a flat skirt drawn multiplied over the ground.
            NONE, POINT, LINE, POLYGON, POLYGON3D, POLYGON3DGROUND
        };

        struct StyleParameters {
            static constexpr int MAX_PARAMETERS = 16;

            int parameterCount;
            std::array<ColorFunction, MAX_PARAMETERS> colorFuncs;
            // Per slot, how much of the colour is emitted rather than lit. 1 = as authored.
            std::array<FloatFunction, MAX_PARAMETERS> emissiveFuncs;
            std::array<FloatFunction, MAX_PARAMETERS> widthFuncs; // for lines, points
            std::array<FloatFunction, MAX_PARAMETERS> offsetFuncs; // for lines, points (stroke width in case of points)
            std::array<FloatFunction, MAX_PARAMETERS> gapWidthFuncs; // for lines: an undrawn gap down the middle
            std::array<FloatFunction, MAX_PARAMETERS> blurFuncs; // for lines: a widened antialias ramp
            // for lines: a casing drawn from the same buffer, one draw before the fill
            std::array<ColorFunction, MAX_PARAMETERS> borderColorFuncs;
            std::array<FloatFunction, MAX_PARAMETERS> borderWidthFuncs;
            std::array<float, MAX_PARAMETERS> strokeScales; // for patterned lines
            // 1 where the slot samples 'pattern', 0 for a plain fill; lines and points keep 1.
            std::array<float, MAX_PARAMETERS> patternScales;
            std::shared_ptr<const BitmapPattern> pattern;
            std::optional<cglib::vec2<float>> translate;
            CompOp compOp;
            int glyphRenderSize;
            // An extrusion's emissive, one uniform for the whole geometry: folding it into the colour
            // on the CPU like emissiveFuncs would grade it and then light it again.
            std::optional<FloatFunction> polygon3DEmissiveFunc;

            StyleParameters() : parameterCount(0), colorFuncs(), emissiveFuncs(), widthFuncs(), offsetFuncs(), gapWidthFuncs(), blurFuncs(), borderColorFuncs(), borderWidthFuncs(), strokeScales(), pattern(), translate(), compOp(CompOp::SRC_OVER), glyphRenderSize(64) { patternScales.fill(1.0f); emissiveFuncs.fill(FloatFunction(1.0f)); }
        };

        // A run of vertices a style parameter can repoint without a decode: styleIndices[1] while the
        // parameter hashes to stateKey, styleIndices[0] otherwise.
        struct FeatureStyleRange {
            std::uint64_t stateKey;
            std::uint32_t firstVertex;
            std::uint32_t vertexCount;
            std::uint8_t styleIndex; // the slot the vertices name right now
            std::uint8_t styleIndices[2];
        };

        // Packed into every extrusion's base slot and recognised by polygon3DVsh; no real ground can take it.
        static constexpr float UNRESOLVED_BASE = -1.0e30f;

        /**
         * One span feature's piece inside this tile; `portal0`/`portal1` tell a real portal from a
         * tile cut, and the renderer unions pieces by `featureId` across tiles. CPU-side only.
         */
        struct SpanRecord {
            long long featureId = 0;
            cglib::vec2<float> p0, p1;                 // in packed vertex space
            bool portal0 = false, portal1 = false;     // an end the tile did NOT cut
            std::size_t vertexOffset = 0, vertexCount = 0;
            // Offset from the chord in metres: a deck hangs under its road, which a negative vertex
            // height cannot express (the shader takes the base only where the height is positive).
            float baseOffset = 0.0f;
        };

        struct VertexGeometryLayoutParameters {
            int vertexSize;
            int dimensions;
            int coordOffset;
            int attribsOffset;
            int texCoordOffset;
            int normalOffset;
            int binormalOffset;
            int heightOffset;
            // Extrusions: the ground under the prism in internal z units, resolved on the CPU so a building
            // across two tiles stays whole. UNRESOLVED_BASE = the ground under this vertex.
            int baseOffset;
            // Span fills and decks: unclamped chord parameter (SpanGeometry::chordParamRaw); the shader
            // discards past the portals. 0.5 until resolved, leaving the deck whole.
            int chordOffset;
            float coordScale;
            float texCoordScale;
            float binormalScale;
            float heightScale;

            VertexGeometryLayoutParameters() : vertexSize(0), dimensions(2), coordOffset(-1), attribsOffset(-1), texCoordOffset(-1), normalOffset(-1), binormalOffset(-1), heightOffset(-1), baseOffset(-1), chordOffset(-1), coordScale(0), texCoordScale(0), binormalScale(0), heightScale(0) { }
        };

        explicit TileGeometry(Type type, float geomScale, const StyleParameters& styleParameters, const VertexGeometryLayoutParameters& vertexGeometryLayoutParameters, VertexArray<std::uint8_t> vertexGeometry, VertexArray<std::uint16_t> indices, std::vector<std::pair<std::size_t, long long>> ids, std::vector<std::pair<std::size_t, std::uint16_t>> geoPosIndexes) : _type(type), _geomScale(geomScale), _styleParameters(styleParameters), _vertexGeometryLayoutParameters(vertexGeometryLayoutParameters), _indicesCount(static_cast<unsigned int>(indices.size())), _vertexGeometry(std::move(vertexGeometry)), _indices(std::move(indices)), _ids(std::move(ids)), _geoPosIndexes(std::move(geoPosIndexes)), _geoPosIndexesCount(static_cast<unsigned int>(indices.size())) { }

        Type getType() const { return _type; }
        float getGeometryScale() const { return _geomScale; }
        const StyleParameters& getStyleParameters() const { return _styleParameters; }
        const VertexGeometryLayoutParameters& getVertexGeometryLayoutParameters() const { return _vertexGeometryLayoutParameters; }
        unsigned int getIndicesCount() const { return _indicesCount; }
        unsigned int getGeoPosIndexesCount() const { return _geoPosIndexesCount; }

        const VertexArray<std::uint8_t>& getVertexGeometry() const { return _vertexGeometry; }
        const VertexArray<std::uint16_t>& getIndices() const { return _indices; }
        const std::vector<std::pair<std::size_t, long long>>& getIds() const { return _ids; }
        const std::vector<std::pair<std::size_t, std::uint16_t>>& getGeoPosIndexes() const { return _geoPosIndexes; }

        const std::vector<FeatureStyleRange>& getFeatureStyleRanges() const { return _featureStyleRanges; }

        void setFeatureStyleRanges(std::vector<FeatureStyleRange> featureStyleRanges, StyleStateRef styleState, std::uint64_t stateKey) {
            _featureStyleRanges = std::move(featureStyleRanges);
            _styleState = std::move(styleState);
            _appliedStateKey = stateKey;
        }

        // Render thread only, before the vertex data is used, so no other thread touches it.
        bool applyStyleState() {
            if (!_styleState) {
                return false;
            }
            std::uint64_t stateKey = _styleState->load(std::memory_order_relaxed);
            if (stateKey == _appliedStateKey) {
                return false;
            }
            _appliedStateKey = stateKey;
            bool changed = false;
            for (std::size_t i = 0; i < _featureStyleRanges.size(); i++) {
                const FeatureStyleRange& range = _featureStyleRanges[i];
                changed = setFeatureStyleIndex(i, range.styleIndices[range.stateKey == stateKey ? 1 : 0]) || changed;
            }
            return changed;
        }

        // True if anything changed; the renderer then re-uploads the dirty byte range.
        bool setFeatureStyleIndex(std::size_t rangeIndex, int styleIndex) {
            FeatureStyleRange& range = _featureStyleRanges.at(rangeIndex);
            if (range.styleIndex == styleIndex || _vertexGeometryLayoutParameters.attribsOffset < 0 || _vertexGeometry.empty()) {
                return false;
            }
            std::size_t vertexSize = _vertexGeometryLayoutParameters.vertexSize;
            std::size_t first = range.firstVertex * vertexSize + _vertexGeometryLayoutParameters.attribsOffset;
            for (std::uint32_t i = 0; i < range.vertexCount; i++) {
                _vertexGeometry[first + i * vertexSize] = static_cast<std::uint8_t>(styleIndex);
            }
            range.styleIndex = static_cast<std::uint8_t>(styleIndex);
            std::size_t last = first + (range.vertexCount > 0 ? (range.vertexCount - 1) * vertexSize : 0) + 1;
            _dirtyVertexBytes = (_dirtyVertexBytes ? std::make_pair(std::min(_dirtyVertexBytes->first, first), std::max(_dirtyVertexBytes->second, last)) : std::make_pair(first, last));
            return true;
        }

        /**
         * Writes the CPU-resolved base (internal z units) of one vertex into the uploaded bytes. Every
         * vertex of a footprint gets the same value, which a shader sample cannot guarantee across tiles.
         */
        bool setVertexBase(std::size_t vertexIndex, float base) {
            return patchVertexFloat(_vertexGeometryLayoutParameters.baseOffset, vertexIndex, base);
        }

        /** The vertex's unclamped chord parameter (see VertexGeometryLayoutParameters::chordOffset). */
        bool setVertexChord(std::size_t vertexIndex, float chordParam) {
            return patchVertexFloat(_vertexGeometryLayoutParameters.chordOffset, vertexIndex, chordParam);
        }

        /** The CPU copy labels are ray-tested against; built before the upload releases the indices. */
        const std::shared_ptr<const ExtrusionOccluder>& getOccluder() const { return _occluder; }
        void setOccluder(std::shared_ptr<const ExtrusionOccluder> occluder) { _occluder = std::move(occluder); }

        /** Whether the bases have been resolved at least once - an extrusion is not drawn before. */
        bool isBaseResolved() const { return _baseResolved; }
        void setBaseResolved(bool resolved) { _baseResolved = resolved; }

        /** The elevation data version the bases were resolved against, so a new DEM tile redoes them. */
        unsigned int getBaseElevationVersion() const { return _baseElevationVersion; }
        void setBaseElevationVersion(unsigned int version) { _baseElevationVersion = version; }

        /** One footprint: its ground sample point, the support vertices for the floor, its tallest vertex in raw height units. */
        struct BaseAnchor {
            cglib::vec2<float> pos;
            std::array<cglib::vec2<float>, ExtrusionFloor::SUPPORT_DIRECTIONS> supports;
            float maxHeightUnits = 0;
            bool haveSupports = false;
        };
        /** A consecutive block of vertices sharing one anchor. */
        struct BaseRun {
            std::uint32_t begin = 0, end = 0, anchorIndex = 0;
        };
        /** Found once from the vertex data, so a DEM arrival re-samples without re-walking (resolveExtrusionBases). */
        const std::vector<BaseAnchor>& getBaseAnchors() const { return _baseAnchors; }
        const std::vector<BaseRun>& getBaseRuns() const { return _baseRuns; }
        void setBaseFootprints(std::vector<BaseAnchor> anchors, std::vector<BaseRun> runs) {
            _baseAnchors = std::move(anchors);
            _baseRuns = std::move(runs);
        }

        /** The span pieces of this tile, empty for anything that is not a SPAN/UNDERGROUND line. */
        const std::vector<SpanRecord>& getSpanRecords() const { return _spanRecords; }
        void setSpanRecords(std::vector<SpanRecord> spanRecords) {
            _spanRecords = std::move(spanRecords);
            _spanRecordChords.assign(_spanRecords.size(), SpanChordRef());
        }
        /**
         * The chord (world portals) each span record last resolved on, so a retained tile with no union
         * this cull still follows its chord's heights instead of freezing.
         */
        struct SpanChordRef {
            cglib::vec2<double> portal0, portal1;
            bool valid = false;
        };
        const SpanChordRef& getSpanRecordChord(std::size_t index) const { return _spanRecordChords[index]; }
        void setSpanRecordChord(std::size_t index, const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            _spanRecordChords[index] = SpanChordRef { portal0, portal1, true };
        }

        /** The cross-tile span union version the chords were resolved against. */
        unsigned int getBaseSpanVersion() const { return _baseSpanVersion; }
        void setBaseSpanVersion(unsigned int version) { _baseSpanVersion = version; }

        const std::optional<std::pair<std::size_t, std::size_t>>& getDirtyVertexBytes() const { return _dirtyVertexBytes; }

        void clearDirtyVertexBytes() { _dirtyVertexBytes.reset(); }

        void releaseVertexArrays() {
            // Kept when style ranges or the extrusion base pass still patch it.
            if (_featureStyleRanges.empty() && _vertexGeometryLayoutParameters.baseOffset < 0) {
                _vertexGeometry.clear();
                _vertexGeometry.shrink_to_fit();
            }
            _indices.clear();
            _indices.shrink_to_fit();
            _ids.clear();
            _ids.shrink_to_fit();
            _geoPosIndexes.clear();
            _geoPosIndexes.shrink_to_fit();
        }


        std::size_t getFeatureCount() const {
            switch (_type) {
            case Type::POINT:
            case Type::LINE:
                return _indicesCount / 6;
            case Type::POLYGON:
            case Type::POLYGON3D:
                return _indicesCount / 3;
            default:
                return 0;
            }
        }

        std::size_t getResidentSize() const {
            return 16 + _vertexGeometry.size() * sizeof(std::uint8_t) + _indices.size() * sizeof(std::uint16_t) + _ids.size() * sizeof(std::pair<std::size_t, long long>);
        }

    private:
        bool patchVertexFloat(int offset, std::size_t vertexIndex, float value) {
            if (offset < 0 || _vertexGeometry.empty()) {
                return false;
            }
            std::size_t first = vertexIndex * _vertexGeometryLayoutParameters.vertexSize + offset;
            float current;
            std::memcpy(&current, &_vertexGeometry[first], sizeof(float));
            if (current == value) {
                return false;
            }
            std::memcpy(&_vertexGeometry[first], &value, sizeof(float));
            std::size_t last = first + sizeof(float);
            _dirtyVertexBytes = (_dirtyVertexBytes ? std::make_pair(std::min(_dirtyVertexBytes->first, first), std::max(_dirtyVertexBytes->second, last)) : std::make_pair(first, last));
            return true;
        }
        const Type _type;
        const float _geomScale;
        const StyleParameters _styleParameters;
        const VertexGeometryLayoutParameters _vertexGeometryLayoutParameters;
        const unsigned int _indicesCount; // real count, even if indices are released
        const unsigned int _geoPosIndexesCount;

        std::vector<FeatureStyleRange> _featureStyleRanges;
        StyleStateRef _styleState;
        std::uint64_t _appliedStateKey = 0;
        std::optional<std::pair<std::size_t, std::size_t>> _dirtyVertexBytes; // byte range to re-upload
        std::shared_ptr<const ExtrusionOccluder> _occluder;
        bool _baseResolved = false;          // extrusions: the CPU ground pass has run at least once
        unsigned int _baseElevationVersion = 0; // ...against this elevation data version
        unsigned int _baseSpanVersion = 0;   // ...and this cross-tile span union version
        std::vector<BaseAnchor> _baseAnchors; // the footprints, found once from the vertex data
        std::vector<BaseRun> _baseRuns;
        std::vector<SpanRecord> _spanRecords; // span lines: one entry per feature piece
        std::vector<SpanChordRef> _spanRecordChords; // ...and the chord each last resolved on in this tile

        VertexArray<std::uint8_t> _vertexGeometry;
        VertexArray<std::uint16_t> _indices;
        std::vector<std::pair<std::size_t, long long>> _ids; // vertex count, feature id
        std::vector<std::pair<std::size_t, std::uint16_t>> _geoPosIndexes; // vertex count, geo point index
    };
}

#endif
