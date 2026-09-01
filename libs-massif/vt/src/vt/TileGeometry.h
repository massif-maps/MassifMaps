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
    // The hash of the value a style parameter currently holds, shared between whoever sets the
    // parameter and the renderer. A feature keeps the hash of the field value that parameter is
    // compared with, so a selection change is a byte rewrite in the vertex data instead of a tile
    // decode. Written by the application thread, read by the render thread.
    using StyleStateRef = std::shared_ptr<const std::atomic<std::uint64_t>>;

    class TileGeometry final {
    public:
        enum class Type {
            // POLYGON3DGROUND is the contact shadow an extrusion casts on the ground it stands on:
            // a flat skirt around the footprint, drawn multiplied over whatever is already there.
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
            std::array<float, MAX_PARAMETERS> strokeScales; // for patterned lines
            // 1 where the slot samples 'pattern', 0 where it is a plain fill. Lines and points
            // leave it at 1 - a line selects its stroke through the stroke atlas instead.
            std::array<float, MAX_PARAMETERS> patternScales;
            std::shared_ptr<const BitmapPattern> pattern;
            std::optional<cglib::vec2<float>> translate;
            CompOp compOp;
            int glyphRenderSize;

            StyleParameters() : parameterCount(0), colorFuncs(), emissiveFuncs(), widthFuncs(), offsetFuncs(), gapWidthFuncs(), blurFuncs(), strokeScales(), pattern(), translate(), compOp(CompOp::SRC_OVER), glyphRenderSize(64) { patternScales.fill(1.0f); emissiveFuncs.fill(FloatFunction(1.0f)); }
        };

        // A run of vertices that a style parameter can repoint, so a feature it picks out repaints
        // instead of the tile being decoded again. The decoder folded the comparison both ways, so
        // both of the styles the run can take are already slots of this geometry: it takes
        // styleIndices[1] while the parameter hashes to stateKey and styleIndices[0] otherwise.
        // One feature owns several runs when the repacking splits its vertices.
        struct FeatureStyleRange {
            std::uint64_t stateKey;
            std::uint32_t firstVertex;
            std::uint32_t vertexCount;
            std::uint8_t styleIndex; // the slot the vertices name right now
            std::uint8_t styleIndices[2];
        };

        // Written into every extrusion's base slot at pack time and recognised by polygon3DVsh.
        // Any value a real ground could take would be indistinguishable from a resolved base.
        static constexpr float UNRESOLVED_BASE = -1.0e30f;

        struct VertexGeometryLayoutParameters {
            int vertexSize;
            int dimensions;
            int coordOffset;
            int attribsOffset;
            int texCoordOffset;
            int normalOffset;
            int binormalOffset;
            int heightOffset;
            // Extrusions only: the ground the prism stands on, in internal z units, resolved on
            // the CPU from the SDK's elevation source and patched in after the fact (see
            // setVertexBase). Sampled in the vertex shader it came from the elevation texture the
            // TILE BEING DRAWN happens to have bound, so one building spanning two tiles got two
            // bases and cracked apart. Starts at UNRESOLVED_BASE, which the shader reads as "use
            // the ground under this vertex" - the pre-CPU behaviour, so a building whose elevation
            // never resolves is drawn slightly wrong rather than not at all.
            int baseOffset;
            float coordScale;
            float texCoordScale;
            float binormalScale;
            float heightScale;

            VertexGeometryLayoutParameters() : vertexSize(0), dimensions(2), coordOffset(-1), attribsOffset(-1), texCoordOffset(-1), normalOffset(-1), binormalOffset(-1), heightOffset(-1), baseOffset(-1), coordScale(0), texCoordScale(0), binormalScale(0), heightScale(0) { }
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

        // Repoints the recorded runs at the style slot the parameter now picks. Called on the
        // render thread before the vertex data is used, so the byte rewrite and the upload of the
        // dirty range happen in the same place and no other thread touches the vertex data.
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

        // Repoints one run at another of the geometry's style slots, in the vertex data that is
        // already uploaded. Returns true if anything changed, in which case the renderer re-uploads
        // the dirty byte range before the next draw.
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
         * Writes the CPU-resolved ground height (metres) of one vertex, the same way a style slot
         * is repointed: patch the bytes already uploaded and grow the dirty range.
         *
         * Every vertex of one footprint gets the SAME value - that is the whole point, and it is
         * what a vertex-shader sample could not guarantee across a tile border.
         */
        bool setVertexBase(std::size_t vertexIndex, float base) {
            if (_vertexGeometryLayoutParameters.baseOffset < 0 || _vertexGeometry.empty()) {
                return false;
            }
            std::size_t vertexSize = _vertexGeometryLayoutParameters.vertexSize;
            std::size_t first = vertexIndex * vertexSize + _vertexGeometryLayoutParameters.baseOffset;
            float current;
            std::memcpy(&current, &_vertexGeometry[first], sizeof(float));
            if (current == base) {
                return false;
            }
            std::memcpy(&_vertexGeometry[first], &base, sizeof(float));
            std::size_t last = first + sizeof(float);
            _dirtyVertexBytes = (_dirtyVertexBytes ? std::make_pair(std::min(_dirtyVertexBytes->first, first), std::max(_dirtyVertexBytes->second, last)) : std::make_pair(first, last));
            return true;
        }

        /** Whether the bases have been resolved at least once - an extrusion is not drawn before. */
        bool isBaseResolved() const { return _baseResolved; }
        void setBaseResolved(bool resolved) { _baseResolved = resolved; }

        /** The elevation data version the bases were resolved against, so a new DEM tile redoes them. */
        unsigned int getBaseElevationVersion() const { return _baseElevationVersion; }
        void setBaseElevationVersion(unsigned int version) { _baseElevationVersion = version; }

        const std::optional<std::pair<std::size_t, std::size_t>>& getDirtyVertexBytes() const { return _dirtyVertexBytes; }

        void clearDirtyVertexBytes() { _dirtyVertexBytes.reset(); }

        void releaseVertexArrays() {
            // The vertex data is what a style slot change patches - and what the extrusion base
            // pass rewrites every time a DEM tile lands, so a base slot pins it too.
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
        bool _baseResolved = false;          // extrusions: the CPU ground pass has run at least once
        unsigned int _baseElevationVersion = 0; // ...against this elevation data version

        VertexArray<std::uint8_t> _vertexGeometry;
        VertexArray<std::uint16_t> _indices;
        std::vector<std::pair<std::size_t, long long>> _ids; // vertex count, feature id
        std::vector<std::pair<std::size_t, std::uint16_t>> _geoPosIndexes; // vertex count, feature id
    };
}

#endif
