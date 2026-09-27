/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_SPANRESOLVER_H_
#define _MASSIF_VT_SPANRESOLVER_H_

#include "TileId.h"
#include "Tile.h"
#include "TileLayer.h"
#include "TileGeometry.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif::vt {
    /**
     * The non-GL state behind 3D bridges: piece grouping, chords, portal heights, per-vertex bases.
     * Not thread-safe: the renderer's mutex guards every call. Off by default, costing nothing: spans
     * drape like the ground and decks stay undrawn. Pure rules live in SpanGeometry.h.
     */
    class SpanResolver final {
    public:
        using ElevationProvider = std::function<bool(const cglib::vec3<double>&, int, bool, double&)>;

        // Which piece a union belongs to: a feature id can be one OSM way carrying several disjoint
        // bridges, so pieces are grouped by connectivity and each group keyed back to the piece asking.
        struct SpanPieceKey {
            TileId tileId = TileId(0, 0, 0);
            long long featureId = 0;
            std::size_t vertexOffset = 0;
            // A bed polygon, a deck and road lines of one feature all start at vertexOffset 0; the type keeps their unions apart.
            TileGeometry::Type type = TileGeometry::Type::NONE;
            bool operator == (const SpanPieceKey& other) const {
                return tileId == other.tileId && featureId == other.featureId && vertexOffset == other.vertexOffset && type == other.type;
            }
            bool operator < (const SpanPieceKey& other) const {
                if (!(tileId == other.tileId)) return tileId < other.tileId;
                if (featureId != other.featureId) return featureId < other.featureId;
                if (vertexOffset != other.vertexOffset) return vertexOffset < other.vertexOffset;
                return type < other.type;
            }
        };

        /**
         * The two portals a span feature runs between, in world coordinates, unioned by feature id over
         * every visible tile holding a piece: no single tile need hold both ends of a long bridge.
         */
        struct SpanUnion {
            cglib::vec2<double> portal0, portal1;
            bool have0 = false, have1 = false;
            // Kept so a label over the deck anchors without repeating the elevation queries.
            double height0 = 0, height1 = 0;
            bool haveHeights = false;
            int zoom = 0; // the tile zoom the pieces came from, for the elevation query
            bool line = false; // a road/rail piece, whose portals sit ON the road (see the merge)
            bool operator == (const SpanUnion& other) const {
                return have0 == other.have0 && have1 == other.have1 && portal0 == other.portal0 && portal1 == other.portal1;
            }
        };
        // The distinct resolved chords with bounds, for the per-vertex label anchor test. Samplers take a
        // copy so the cull thread can anchor labels with the lock released.
        struct SpanChord {
            cglib::vec2<double> portal0, portal1;
            double height0 = 0, height1 = 0;
            cglib::vec2<double> boundsMin, boundsMax;
        };

        void setEnabled(bool enabled);
        bool isEnabled() const { return _enabled; }
        // The ground under a point, in internal z units, at a tile zoom; false when there is no
        // data there yet (the renderer's extrusion provider).
        void setElevationProvider(ElevationProvider provider);
        void setMetersToInternal(double metersToInternal);

        // `visibleTileIds`: the tiles whose DEM level a portal is read at; `baseVersion`: the reads' elevation version.
        void build(const std::map<TileId, std::shared_ptr<const Tile>>& tiles, const std::vector<std::shared_ptr<const Tile>>& spanReferenceTiles, const std::set<TileId>& visibleTileIds, unsigned int baseVersion);
        // True once after a build that gave a chord its heights for the first time: a label that
        // could not reach the deck before can now.
        bool takeLabelsDirty();
        // Writes every vertex base of one span geometry from its chord. False while there is no chord
        // or no ground yet; the caller decides how an unresolved piece looks.
        bool resolve(const TileId& sourceTileId, const std::shared_ptr<TileGeometry>& geometry, unsigned int baseVersion) const;
        // The distinct chords with heights, for label anchoring; re-read when the version moved.
        const std::vector<SpanChord>& chords(unsigned int baseVersion) const;
        static bool chordHeightAt(const std::vector<SpanChord>& chords, const cglib::vec2<double>& pos, double& height);
        // The deck height over a point, from the chords as last built.
        bool heightAt(const cglib::vec2<double>& pos, double& height) const;
        // The cut ends the last build could not chord, with the zoom to fetch their tile at.
        const std::vector<std::pair<int, cglib::vec2<double>>>& unresolvedEnds() const;
        unsigned int unionVersion() const { return _spanUnionVersion.load(std::memory_order_relaxed); }

        // Tile space to normalized world: a pure function of the tile id, shared with the renderer.
        static cglib::mat3x3<double> tileMatrix2D(const TileId& tileId, float coordScale = 1.0f);

    private:
        // Kept after its tiles leave the view, or the chord shrinks to what is still loaded. Heights
        // live here since unions are rebuilt every cull.
        struct CachedChord {
            cglib::vec2<double> portal0, portal1;
            std::uint64_t stamp = 0;
            double height0 = 0, height1 = 0;
            bool haveHeights = false;
            unsigned int sampledCull = 0; // the buildSpanUnions pass that last read it
            // The elevation version the pair was read at, so an exaggeration change re-reads it.
            unsigned int baseVersion = 0;
        };
        // Keyed by the portals; a tilted city view holds over a thousand chords in use (bound in rememberChord).
        using ChordKey = std::array<double, 4>;
        static ChordKey chordKey(const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            return ChordKey { portal0(0), portal0(1), portal1(0), portal1(1) };
        }
        CachedChord& rememberChord(const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1);
        // The finest visible tile's zoom at the point (its DEM level draws the road); `fallbackZoom` otherwise.
        int spanSampleZoomAt(const cglib::vec2<double>& pos, int fallbackZoom) const;
        // False, chord unchanged, when a portal's DEM is missing, so the last good pair is kept.
        bool sampleChordHeights(CachedChord& chord, int pieceZoom) const;
        void rebuildSpanChords() const;

        bool _enabled = false;
        ElevationProvider _elevationProvider;
        double _metersToInternal = 0;
        std::set<TileId> _visibleTileIds;
        mutable unsigned int _baseVersion = 0; // the elevation version of the last call
        bool _labelsDirty = false;

        // Written by build, read by resolve during the frame.
        mutable std::map<SpanPieceKey, SpanUnion> _spanUnions; // heights filled by the resolve
        mutable std::vector<SpanChord> _spanChords;
        mutable unsigned int _spanChordsBaseVersion = 0; // the elevation version _spanChords was built at
        std::atomic<unsigned int> _spanUnionVersion { 0 };
        unsigned int _spanCullSerial = 0;
        mutable std::map<ChordKey, CachedChord> _spanChordCache;
        std::uint64_t _spanChordClock = 0;
        std::vector<std::pair<int, cglib::vec2<double>>> _unresolvedSpanEnds;
    };
}

#endif
