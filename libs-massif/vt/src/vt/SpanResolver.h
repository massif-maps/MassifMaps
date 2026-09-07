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
     * Everything behind a 3D bridge that is not drawing: which pieces of which tiles are one
     * structure, the chord between its two portals, the ground read at each portal, the per-vertex
     * base along the chord, and the chords a label asks about. GLTileRenderer owns one and calls
     * it at three moments - the cull (build), the frame (resolve, one geometry at a time) and the
     * label anchoring (chords) - and keeps the GL side: the span drape, the SPAN shader flags, the
     * rule that an unresolved deck is not drawn.
     *
     * NOT thread-safe on its own: the renderer's mutex guards every call, cull thread and render
     * thread alike, as it did when this was renderer state. The pure rules live in SpanGeometry.h;
     * this class is the state machine around them, and it can be driven with a fake elevation
     * provider (tests/vt/SpanResolverTest.cpp).
     *
     * Off (the default), build and resolve return before touching a piece: a span drapes like the
     * ground, a deck stays undrawn, and a map that never turns it on pays nothing.
     */
    class SpanResolver final {
    public:
        using ElevationProvider = std::function<bool(const cglib::vec3<double>&, int, bool, double&)>;

        // Which PIECE a union belongs to. A feature id is a whole OSM way and carries several
        // disjoint bridges, so the id alone spans the gaps between them - measured 7.1 km against
        // a 3.8 km bridge. The pieces are grouped by connectivity first, and each group is keyed
        // back to the piece that asks for it.
        struct SpanPieceKey {
            TileId tileId = TileId(0, 0, 0);
            long long featureId = 0;
            std::size_t vertexOffset = 0;
            // One structure is several GEOMETRIES of the same feature in the same tile - a bridge
            // is a bed polygon, an extruded deck and the road lines on it - and they all start at
            // vertexOffset 0, so without the type they share a key and overwrite each other's
            // union. They must not: a ring's two ends (farthest apart) are not a line's two ends,
            // so the survivor resolved the others against the wrong chord and the deck broke back
            // into per-tile pieces.
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
         * The two PORTALS a span feature runs between, in world coordinates, unioned over every
         * visible tile holding a piece of it. The tile grid cuts a long bridge into pieces and no
         * single one holds both ends - Millau is 3.7 km of bridge against ~3.5 km at z13 - so the
         * portals are collected by feature id, which mapbox tiles keep stable across tiles.
         */
        struct SpanUnion {
            cglib::vec2<double> portal0, portal1;
            bool have0 = false, have1 = false;
            // The chord's resolved ground heights, kept so a LABEL over the deck can be anchored
            // to it without paying the elevation queries again.
            double height0 = 0, height1 = 0;
            bool haveHeights = false;
            int zoom = 0; // the tile zoom the pieces came from, for the elevation query
            bool line = false; // a road/rail piece, whose portals sit ON the road (see the merge)
            bool operator == (const SpanUnion& other) const {
                return have0 == other.have0 && have1 == other.have1 && portal0 == other.portal0 && portal1 == other.portal1;
            }
        };
        // The DISTINCT resolved chords of _spanUnions with their bounds: a label anchor asks
        // "is this vertex on a deck" per vertex, and against the unions that was one chord test
        // per piece - hundreds in a city, most of them the same chord - for every vertex of every
        // label. Rebuilt wherever the unions gain a chord or a height; a sampler takes a COPY,
        // so the cull thread can anchor labels with the renderer's lock released.
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

        // The cull: group the pieces of every tile (and of the reference tiles fetched for
        // stranded pieces) into structures, resolve their chords, read the portal heights.
        // `visibleTileIds` names the tiles whose DEM level a portal is read at; `baseVersion` is
        // the elevation version the reads belong to.
        void build(const std::map<TileId, std::shared_ptr<const Tile>>& tiles, const std::vector<std::shared_ptr<const Tile>>& spanReferenceTiles, const std::set<TileId>& visibleTileIds, unsigned int baseVersion);
        // True once after a build that gave a chord its heights for the first time: a label that
        // could not reach the deck before can now.
        bool takeLabelsDirty();
        // The frame: write every vertex base of one span geometry from its chord. False while the
        // piece has no chord or the ground has not answered; the caller decides what an
        // unresolved piece looks like (a line drapes, a deck is skipped).
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
        // A chord that was resolved once, kept after the tiles that proved it left the view. A
        // bridge's portals are a property of the WORLD, not of what is on screen: zooming into one
        // end drops the far piece from the visible set, and without this the chord shortens to
        // whatever is still loaded and the deck visibly changes angle.
        // The heights live HERE, with the chord, not with the pieces standing on it. A union is
        // rebuilt from scratch every cull and keyed by the piece's tile, so a piece whose tile
        // just entered the view had no previous heights to keep and hid until the DEM under a
        // portal answered - which, for a portal off screen, could be never. A chord is one pair
        // of heights whatever tile asks, and it keeps the last pair it resolved.
        struct CachedChord {
            cglib::vec2<double> portal0, portal1;
            std::uint64_t stamp = 0;
            double height0 = 0, height1 = 0;
            bool haveHeights = false;
            unsigned int sampledCull = 0; // the buildSpanUnions pass that last read it
            // The elevation version the pair was read at. An exaggeration ramp (the auto-flatten)
            // moves the ground every frame and buildings re-resolve on each bump; a chord read
            // once at a cull stayed at its 3D height while the ground sank under it, and a deck
            // flattened at zero stayed on the water once the ground came back.
            unsigned int baseVersion = 0;
        };
        // Keyed by the portals: a city view at a tilt holds well over a thousand distinct chords
        // (every feature of every structure, per zoom group), and a bound of 512 evicted chords
        // still in use every cull - each came back without its heights, and its deck hid.
        using ChordKey = std::array<double, 4>;
        static ChordKey chordKey(const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            return ChordKey { portal0(0), portal0(1), portal1(0), portal1(1) };
        }
        // The zoom of the finest visible tile holding the point, which is the tile whose DEM level
        // the road at a portal is drawn with; `fallbackZoom` for a point in no visible tile.
        // Read a chord's portal heights, each at its own tile's zoom, a portal off screen at the
        // asking piece's. False and the chord unchanged when a portal's DEM is not there: the
        // last good pair is kept.
        CachedChord& rememberChord(const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1);
        int spanSampleZoomAt(const cglib::vec2<double>& pos, int fallbackZoom) const;
        bool sampleChordHeights(CachedChord& chord, int pieceZoom) const;
        void rebuildSpanChords() const;

        bool _enabled = false;
        ElevationProvider _elevationProvider;
        double _metersToInternal = 0;
        std::set<TileId> _visibleTileIds;
        mutable unsigned int _baseVersion = 0; // the elevation version of the last call
        bool _labelsDirty = false;

        // Written by build, read by resolve during the frame - the same build-then-consume
        // pattern the render tiles use.
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
