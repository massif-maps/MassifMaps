/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#include "SpanResolver.h"
#include "SpanGeometry.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace massif::vt {
    void SpanResolver::setEnabled(bool enabled) {
        if (_enabled == enabled) {
            return;
        }
        _enabled = enabled;
        // Turned off: forget every chord, so labels and decks stop asking, and let the next build
        // start from nothing when turned on again.
        _spanUnions.clear();
        _spanChords.clear();
        _spanChordCache.clear();
        _unresolvedSpanEnds.clear();
        _spanUnionVersion.fetch_add(1, std::memory_order_relaxed);
    }

    void SpanResolver::setElevationProvider(ElevationProvider provider) {
        _elevationProvider = std::move(provider);
    }

    void SpanResolver::setMetersToInternal(double metersToInternal) {
        _metersToInternal = metersToInternal;
    }

    bool SpanResolver::takeLabelsDirty() {
        bool dirty = _labelsDirty;
        _labelsDirty = false;
        return dirty;
    }

    const std::vector<SpanResolver::SpanChord>& SpanResolver::chords(unsigned int baseVersion) const {
        _baseVersion = baseVersion;
        // Rebuilt when the ground moved since, re-reading the entries as it goes.
        if (!_spanChords.empty() && _spanChordsBaseVersion != _baseVersion) {
            rebuildSpanChords();
        }
        return _spanChords;
    }

    bool SpanResolver::heightAt(const cglib::vec2<double>& pos, double& height) const {
        return chordHeightAt(_spanChords, pos, height);
    }

    const std::vector<std::pair<int, cglib::vec2<double>>>& SpanResolver::unresolvedEnds() const {
        return _unresolvedSpanEnds;
    }

    cglib::mat3x3<double> SpanResolver::tileMatrix2D(const TileId& tileId, float coordScale) {
        double z = 1.0 / (1 << tileId.zoom);
        cglib::mat3x3<double> m = cglib::mat3x3<double>::zero();
        m(0, 0) = z * coordScale;
        m(1, 1) = -z * coordScale;
        m(2, 2) = 1;
        m(0, 2) = tileId.x * z - 0.5;
        m(1, 2) = ((1 << tileId.zoom) - tileId.y) * z - 0.5;
        return m;
    }

    void SpanResolver::build(const std::map<TileId, std::shared_ptr<const Tile>>& tiles, const std::vector<std::shared_ptr<const Tile>>& spanReferenceTiles, const std::set<TileId>& visibleTileIds, unsigned int baseVersion) {
        if (!_enabled) {
            return; // 3D bridges off: no unions, no chords, no reference tiles asked for
        }
        _visibleTileIds = visibleTileIds;
        _baseVersion = baseVersion;
        // One piece of one span, in world coordinates. `portalN` marks an end the tile did NOT cut.
        struct SpanPiece {
            cglib::vec2<double> e0, e1;
            bool portal0 = false, portal1 = false;
            SpanPieceKey key;
        };

        // Grouped by ZOOM alone, not by feature id: the symbolizer passes a TILE-LOCAL id, so the three
        // tiles holding one deck give its OSM way three unrelated ids. Zoom still separates them -
        // while tiles load, the same bridge is present at two, with different simplifications.
        std::map<int, std::vector<SpanPiece>> piecesByZoom;
        std::set<const Tile*> visited;
        // The reference tiles first: at the source's max zoom they hold a piece UNCUT by the
        // overzoomed targets on screen, and the visited set then skips a visible tile that is
        // the same object.
        std::vector<std::shared_ptr<const Tile>> spanTiles(spanReferenceTiles);
        for (auto it = tiles.begin(); it != tiles.end(); it++) {
            spanTiles.push_back(it->second);
        }
        for (const std::shared_ptr<const Tile>& tile : spanTiles) {
            if (!tile || !visited.insert(tile.get()).second) {
                continue;
            }
            // The records are in the SOURCE tile's space, the one the geometry was built in.
            cglib::mat3x3<double> tileMatrix = tileMatrix2D(tile->getTileId(), 1.0f);
            for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
                if (!layer->hasSpanGeometry()) {
                    continue; // a style with no elevation-mode never gets past here
                }
                for (const std::shared_ptr<TileGeometry>& geometry : layer->getGeometries()) {
                    for (const TileGeometry::SpanRecord& record : geometry->getSpanRecords()) {
                        SpanPiece piece;
                        piece.e0 = cglib::transform_point(cglib::vec2<double>(record.p0(0), 1.0 - record.p0(1)), tileMatrix);
                        piece.e1 = cglib::transform_point(cglib::vec2<double>(record.p1(0), 1.0 - record.p1(1)), tileMatrix);
                        piece.portal0 = record.portal0;
                        piece.portal1 = record.portal1;
                        piece.key = SpanPieceKey { tile->getTileId(), record.featureId, record.vertexOffset, geometry->getType() };
                        piecesByZoom[tile->getTileId().zoom].push_back(piece);
                    }
                }
            }
        }

        // Two pieces are the same structure when their CUT ends meet. The source's buffer makes
        // neighbouring copies OVERLAP rather than touch, so this is a proximity test over a fraction of
        // a tile. Only a cut continues into another piece; a real portal ends the run.
        auto meets = [](const SpanPiece& a, const SpanPiece& b, double tolerance2) {
            return SpanGeometry::piecesMeet(a.e0, a.e1, a.portal0, a.portal1,
                                            b.e0, b.e1, b.portal0, b.portal1, tolerance2);
        };
        // Keep the two portals FARTHEST apart: a structure can enter and leave the same tile, and
        // it is the outermost pair the deck spans between.
        auto addPortal = [](SpanUnion& span, const cglib::vec2<double>& p) {
            if (!span.have0) {
                span.portal0 = p;
                span.have0 = true;
                return;
            }
            if (!span.have1) {
                if (p != span.portal0) {
                    span.portal1 = p;
                    span.have1 = true;
                }
                return;
            }
            double best = cglib::norm(span.portal1 - span.portal0);
            if (cglib::norm(p - span.portal0) > best) {
                span.portal1 = p;
            }
            else if (cglib::norm(p - span.portal1) > best) {
                span.portal0 = p;
            }
        };

        std::vector<std::pair<int, cglib::vec2<double>>> unresolvedEnds;
        _spanCullSerial++;

        std::map<SpanPieceKey, SpanUnion> spanUnions;
        for (auto it = piecesByZoom.begin(); it != piecesByZoom.end(); it++) {
            const std::vector<SpanPiece>& pieces = it->second;
            // A fraction of a tile at this zoom: the overlap is the source's own buffer, so the
            // tolerance scales with the tile, not with the ground.
            double tolerance = 0.1 / (1 << it->first);
            double tolerance2 = tolerance * tolerance;
            std::vector<std::size_t> group(pieces.size());
            for (std::size_t i = 0; i < group.size(); i++) {
                group[i] = i;
            }
            std::function<std::size_t(std::size_t)> root = [&group, &root](std::size_t i) {
                return group[i] == i ? i : (group[i] = root(group[i]));
            };
            // Bucketed by end, a cell per tolerance: two pieces can only meet within the tolerance, so
            // the candidates are those with an end in the 3x3 cells around each of this one's. The
            // quadratic pass took a second per build once a city's every bridge arrived at z14.
            auto cellKey = [tolerance](const cglib::vec2<double>& p, int dx, int dy) -> long long {
                long long cx = static_cast<long long>(std::floor(p(0) / tolerance)) + dx;
                long long cy = static_cast<long long>(std::floor(p(1) / tolerance)) + dy;
                return (cx << 32) ^ (cy & 0xffffffffLL);
            };
            std::unordered_map<long long, std::vector<std::size_t>> cells;
            for (std::size_t i = 0; i < pieces.size(); i++) {
                cells[cellKey(pieces[i].e0, 0, 0)].push_back(i);
                cells[cellKey(pieces[i].e1, 0, 0)].push_back(i);
            }
            for (std::size_t i = 0; i < pieces.size(); i++) {
                for (const cglib::vec2<double>& end : { pieces[i].e0, pieces[i].e1 }) {
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            auto cellIt = cells.find(cellKey(end, dx, dy));
                            if (cellIt == cells.end()) {
                                continue;
                            }
                            for (std::size_t j : cellIt->second) {
                                if (j > i && meets(pieces[i], pieces[j], tolerance2)) {
                                    group[root(i)] = root(j);
                                }
                            }
                        }
                    }
                }
            }

            std::map<std::size_t, SpanUnion> groupUnions;
            std::map<std::size_t, std::vector<cglib::vec2<double>>> groupEnds;
            for (std::size_t i = 0; i < pieces.size(); i++) {
                if (pieces[i].portal0) {
                    addPortal(groupUnions[root(i)], pieces[i].e0);
                }
                if (pieces[i].portal1) {
                    addPortal(groupUnions[root(i)], pieces[i].e1);
                }
                std::vector<cglib::vec2<double>>& ends = groupEnds[root(i)];
                ends.push_back(pieces[i].e0);
                ends.push_back(pieces[i].e1);
            }
            // How far the group's own geometry reaches, which is what its chord has to span.
            std::map<std::size_t, double> groupDiameters2;
            for (auto endsIt = groupEnds.begin(); endsIt != groupEnds.end(); endsIt++) {
                const std::vector<cglib::vec2<double>>& ends = endsIt->second;
                double diameter2 = 0;
                for (std::size_t a = 0; a < ends.size(); a++) {
                    for (std::size_t b = a + 1; b < ends.size(); b++) {
                        diameter2 = std::max(diameter2, cglib::norm(ends[a] - ends[b]));
                    }
                }
                groupDiameters2[endsIt->first] = diameter2;
            }
            for (std::size_t i = 0; i < pieces.size(); i++) {
                auto groupIt = groupUnions.find(root(i));
                SpanUnion span;
                if (groupIt != groupUnions.end()) {
                    span = groupIt->second;
                }
                // Both ends of ONE abutment, seen in two neighbouring tiles, read as two portals -
                // and their 45 m chord passed the have0/have1 test, so the far portal being off
                // screen looked resolved and sank a 1.3 km deck instead of borrowing its chord.
                if (span.have0 && span.have1
                 && !SpanGeometry::chordSpansGroup(cglib::norm(span.portal1 - span.portal0), groupDiameters2[root(i)])) {
                    span = SpanUnion();
                }
                if (!span.have0 || !span.have1) {
                    // Lend it a chord resolved earlier, probed with the piece's own MIDPOINT: a piece in
                    // the middle of a long bridge is cut at both ends and has no portal to offer, and
                    // those are exactly the ones stranded when the far end is off screen.
                    auto chordIt = SpanGeometry::borrowChord(pieces[i].e0, pieces[i].portal0, pieces[i].e1, pieces[i].portal1, _spanChordCache.begin(), _spanChordCache.end(),
                                                             [](auto it) -> const CachedChord& { return it->second; });
                    if (chordIt != _spanChordCache.end()) {
                        span.portal0 = chordIt->second.portal0;
                        span.portal1 = chordIt->second.portal1;
                        span.have0 = span.have1 = true;
                    }
                }
                span.zoom = pieces[i].key.tileId.zoom;
                // Remembered AFTER the merge below, winners only: stamped here, every copy's chord
                // read as an incumbent and the merge had nothing to prefer.
                if (!span.have0 || !span.have1) {
                    // Still no chord: name the tiles its far ends are in, for the owner to fetch.
                    if (!pieces[i].portal0) {
                        unresolvedEnds.emplace_back(span.zoom, SpanGeometry::beyondCutEnd(pieces[i].e0, pieces[i].e1, span.zoom));
                    }
                    if (!pieces[i].portal1) {
                        unresolvedEnds.emplace_back(span.zoom, SpanGeometry::beyondCutEnd(pieces[i].e1, pieces[i].e0, span.zoom));
                    }
                }
                span.line = (pieces[i].key.type == TileGeometry::Type::LINE);
                spanUnions[pieces[i].key] = span;
            }
        }
        _unresolvedSpanEnds = std::move(unresolvedEnds);

        // Spans that start and end together are one structure and share one chord: a dual carriageway
        // is two features side by side, and the SAME deck seen from two source tiles is clipped
        // differently by each. LONGEST FIRST, so the copy with the better-placed ends is kept.
        constexpr double PAIR_TOLERANCE = 100.0 / 40075017.0; // 100 m, in normalized world units
        {
            std::vector<SpanUnion*> resolved;
            for (auto it = spanUnions.begin(); it != spanUnions.end(); it++) {
                if (it->second.have0 && it->second.have1) {
                    resolved.push_back(&it->second);
                }
            }
            // An INCUMBENT chord first - one the cache already holds with heights - then a ROAD's, whose
            // portals are the feature's ends where the approach is draped, and only then the longest.
            // Longest-first alone crowned a different chord each cull, so the deck jumped.
            auto incumbent = [this](const SpanUnion* span) -> std::uint64_t {
                auto it = _spanChordCache.find(chordKey(span->portal0, span->portal1));
                return it != _spanChordCache.end() && it->second.haveHeights ? it->second.stamp : 0;
            };
            std::stable_sort(resolved.begin(), resolved.end(), [&incumbent](const SpanUnion* a, const SpanUnion* b) {
                if (a->line != b->line) {
                    return a->line;
                }
                std::uint64_t ia = incumbent(a), ib = incumbent(b);
                if ((ia != 0) != (ib != 0)) {
                    return ia != 0;
                }
                if (ia != ib) {
                    return ia > ib;
                }
                return cglib::norm(a->portal1 - a->portal0) > cglib::norm(b->portal1 - b->portal0);
            });
            std::vector<SpanUnion*> merged;
            for (SpanUnion* span : resolved) {
                double tolerance2 = PAIR_TOLERANCE * PAIR_TOLERANCE;
                cglib::vec2<double> mid = (span->portal0 + span->portal1) * 0.5;
                double length2 = cglib::norm(span->portal1 - span->portal0);
                SpanUnion* match = nullptr;
                for (SpanUnion* candidate : merged) {
                    if (SpanGeometry::chordLiesOn(span->portal0, span->portal1, candidate->portal0, candidate->portal1)) {
                        match = candidate;
                        break;
                    }
                    // Their ENDS are staggered - each carriageway's bridge is tagged over a slightly
                    // different chainage - but their middles and lengths are not.
                    cglib::vec2<double> candidateMid = (candidate->portal0 + candidate->portal1) * 0.5;
                    double candidateLength2 = cglib::norm(candidate->portal1 - candidate->portal0);
                    if (cglib::norm(candidateMid - mid) < tolerance2 && length2 > 0 && candidateLength2 > 0) {
                        double ratio = length2 / candidateLength2;
                        if (ratio > 0.8 && ratio < 1.25) {
                            match = candidate;
                            break;
                        }
                    }
                }
                if (match) {
                    *span = *match; // one chord for both decks
                }
                else {
                    merged.push_back(span);
                }
            }
        }

        // Resolve the chord heights NOW, not when the geometry is drawn: labels re-anchor in startFrame,
        // before any geometry resolves. Per CHORD, on its cache entry, so every piece reads the same
        // pair and one that did not resolve this time keeps what the chord had.
        bool gainedHeights = false;
        bool changed = (spanUnions != _spanUnions);
        for (auto it = spanUnions.begin(); it != spanUnions.end(); it++) {
            SpanUnion& span = it->second;
            if (!span.have0 || !span.have1) {
                continue;
            }
            CachedChord& chord = rememberChord(span.portal0, span.portal1);
            bool had = chord.haveHeights;
            // Once per cull per chord: the unions run coarsest tile first, so a portal off screen
            // is read at the coarsest piece's zoom, where its DEM is likeliest to be cached.
            bool sampled = chord.sampledCull == _spanCullSerial ? chord.haveHeights : sampleChordHeights(chord, span.zoom);
            chord.sampledCull = _spanCullSerial;
            if (sampled && !had) {
                gainedHeights = true;
            }
            span.height0 = chord.height0;
            span.height1 = chord.height1;
            span.haveHeights = chord.haveHeights;
            auto oldIt = _spanUnions.find(it->first);
            if (oldIt == _spanUnions.end() || oldIt->second.haveHeights != span.haveHeights
             || oldIt->second.height0 != span.height0 || oldIt->second.height1 != span.height1) {
                changed = true;
            }
        }
        if (changed) {
            _spanUnions = std::move(spanUnions);
            rebuildSpanChords();
            _spanUnionVersion.fetch_add(1, std::memory_order_relaxed);
        }
        if (gainedHeights) {
            _labelsDirty = true; // a deck a label could not reach before
        }
    }

    SpanResolver::CachedChord& SpanResolver::rememberChord(const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
        // Remember a resolved chord and lend it back to a piece whose far end has left the view: the
        // coarser reference tiles resolve here too, and the zoom groups run coarsest first. Bounded well
        // above what a view holds - an evicted chord comes back without its heights.
        constexpr std::size_t MAX_CACHED_CHORDS = 4096;
        auto it = _spanChordCache.find(chordKey(portal0, portal1));
        if (it != _spanChordCache.end()) {
            it->second.stamp = ++_spanChordClock;
            return it->second;
        }
        if (_spanChordCache.size() >= MAX_CACHED_CHORDS) {
            auto oldest = std::min_element(_spanChordCache.begin(), _spanChordCache.end(),
                [](const auto& a, const auto& b) { return a.second.stamp < b.second.stamp; });
            _spanChordCache.erase(oldest);
        }
        CachedChord chord;
        chord.portal0 = portal0;
        chord.portal1 = portal1;
        chord.stamp = ++_spanChordClock;
        return _spanChordCache.emplace(chordKey(portal0, portal1), chord).first->second;
    }

    int SpanResolver::spanSampleZoomAt(const cglib::vec2<double>& pos, int fallbackZoom) const {
        // The tile drawn under the portal is the one whose DEM level the approach road is draped with,
        // and the deck has to meet that road - the finest of them where a stand-in parent is still up.
        // Off screen there is no road to meet, and the finest visible zoom asks for a far lidar tile.
        int zoom = -1;
        for (const TileId& tileId : _visibleTileIds) {
            if (tileId.zoom <= zoom) {
                continue;
            }
            int extent = 1 << tileId.zoom;
            int x = static_cast<int>(std::floor((pos(0) + 0.5) * extent));
            int y = static_cast<int>(std::floor((0.5 - pos(1)) * extent));
            if (((tileId.x - x) % extent) == 0 && tileId.y == y) {
                zoom = tileId.zoom;
            }
        }
        return zoom >= 0 ? zoom : fallbackZoom;
    }

    bool SpanResolver::sampleChordHeights(CachedChord& chord, int pieceZoom) const {
        if (!_elevationProvider) {
            return false;
        }
        // The drawn surface (not the smoothed field a building's base uses), each portal at its
        // own tile's zoom. Re-read every time - a finer DEM landing moves the road the deck must
        // meet - and kept as it was when a portal's DEM is not there.
        double h0 = 0, h1 = 0;
        if (!_elevationProvider(cglib::vec3<double>(chord.portal0(0), chord.portal0(1), 0), spanSampleZoomAt(chord.portal0, pieceZoom), false, h0)
         || !_elevationProvider(cglib::vec3<double>(chord.portal1(0), chord.portal1(1), 0), spanSampleZoomAt(chord.portal1, pieceZoom), false, h1)) {
            return false;
        }
        chord.height0 = h0;
        chord.height1 = h1;
        chord.haveHeights = true;
        chord.baseVersion = _baseVersion;
        return true;
    }

    void SpanResolver::rebuildSpanChords() const {
        _spanChordsBaseVersion = _baseVersion;
        _spanChords.clear();
        for (auto it = _spanUnions.begin(); it != _spanUnions.end(); it++) {
            const SpanUnion& span = it->second;
            if (!span.haveHeights) {
                continue;
            }
            bool known = false;
            for (const SpanChord& chord : _spanChords) {
                if (chord.portal0 == span.portal0 && chord.portal1 == span.portal1) {
                    known = true;
                    break;
                }
            }
            if (known) {
                continue;
            }
            SpanChord chord;
            chord.portal0 = span.portal0;
            chord.portal1 = span.portal1;
            chord.height0 = span.height0;
            chord.height1 = span.height1;
            // The entry's pair when it has one: the union's copy is what the cull saw, the entry
            // is re-read every ramp frame by the pieces on it.
            auto entryIt = _spanChordCache.find(chordKey(span.portal0, span.portal1));
            if (entryIt != _spanChordCache.end()) {
                // Re-read HERE, not by the pieces on the draw: labels anchor before any deck resolves in
                // the frame, so a rebuild from last frame's entries kept every POI one ramp step behind
                // the deck - and there for good after the ramp's last frame.
                if (entryIt->second.baseVersion != _spanChordsBaseVersion) {
                    sampleChordHeights(entryIt->second, span.zoom);
                }
                if (entryIt->second.haveHeights) {
                    chord.height0 = entryIt->second.height0;
                    chord.height1 = entryIt->second.height1;
                }
            }
            // The bounds isOnChord could ever accept: the portals, out by the match allowance.
            double allowance = SpanGeometry::matchAllowance(cglib::length(span.portal1 - span.portal0));
            cglib::vec2<double> margin(allowance, allowance);
            chord.boundsMin = cglib::vec2<double>(std::min(span.portal0(0), span.portal1(0)), std::min(span.portal0(1), span.portal1(1))) - margin;
            chord.boundsMax = cglib::vec2<double>(std::max(span.portal0(0), span.portal1(0)), std::max(span.portal0(1), span.portal1(1))) + margin;
            _spanChords.push_back(chord);
        }
    }

    bool SpanResolver::chordHeightAt(const std::vector<SpanChord>& chords, const cglib::vec2<double>& pos, double& height) {
        for (const SpanChord& chord : chords) {
            if (pos(0) < chord.boundsMin(0) || pos(0) > chord.boundsMax(0) || pos(1) < chord.boundsMin(1) || pos(1) > chord.boundsMax(1)) {
                continue;
            }
            if (!SpanGeometry::isOnChord(pos, chord.portal0, chord.portal1)) {
                continue;
            }
            height = SpanGeometry::chordHeight(chord.height0, chord.height1,
                                               SpanGeometry::chordParam(pos, chord.portal0, chord.portal1));
            return true;
        }
        return false;
    }

    bool SpanResolver::resolve(const TileId& sourceTileId, const std::shared_ptr<TileGeometry>& geometry, unsigned int baseVersion) const {
        const TileGeometry::VertexGeometryLayoutParameters& params = geometry->getVertexGeometryLayoutParameters();
        const std::vector<TileGeometry::SpanRecord>& spanRecords = geometry->getSpanRecords();
        if (params.baseOffset < 0 || spanRecords.empty() || !_elevationProvider) {
            return true; // not a span, or no elevation at all - the line stays on the ground
        }
        if (!_enabled) {
            return false; // 3D bridges off: never resolved, so a line drapes and a deck is not drawn
        }
        _baseVersion = baseVersion;
        unsigned int version = _baseVersion;
        unsigned int spanVersion = _spanUnionVersion.load(std::memory_order_relaxed);
        if (geometry->isBaseResolved() && geometry->getBaseElevationVersion() == version && geometry->getBaseSpanVersion() == spanVersion) {
            return true;
        }
        const VertexArray<std::uint8_t>& vertexGeometry = geometry->getVertexGeometry();
        if (vertexGeometry.empty() || params.vertexSize <= 0) {
            return false;
        }

        // On the CPU because it must be TILE-INDEPENDENT. Sampling the two ends from the elevation
        // texture of the tile being drawn only works while they are inside it, and a long bridge's
        // portals sit well outside the tile its middle lands in - that is the whole case here.
        cglib::mat3x3<double> tileMatrix = tileMatrix2D(sourceTileId, 1.0f);
        std::size_t vertexCount = vertexGeometry.size() / params.vertexSize;
        bool allResolved = true;
        // A record's baseOffset is in metres and the chord in internal z units: metres to world z at the
        // equator, then the mercator stretch at the vertex's latitude. WITHOUT the exaggeration - the
        // shader adds the thickness back unexaggerated, so the two only cancel at exaggeration 1.
        double metersToInternal = _metersToInternal;
        auto baseOffsetAt = [&](const cglib::vec2<double>& w, float metres) -> double {
            return metres * metersToInternal * std::cosh(6.283185307179586 * w(1)); // 2 pi: normalized world y to the mercator angle
        };
        // Every vertex or none: a record covers the run that carried the same span info, and an
        // EXTRUSION has vertices no record reaches - walls and skirt are emitted around the ring. Those
        // kept the sentinel and stayed on the ground, fanning the deck across the valley.
        std::vector<bool> patched(vertexCount, false);
        // A piece drawn from a tile no longer in the set - retained while its replacement loads - has no
        // union this cull. Its bases from the last resolve are still in the vertices and still right, so
        // it keeps them rather than hiding for the hold.
        bool wasResolved = geometry->isBaseResolved();
        // A chord whose re-read failed this frame draws on its last pair but is asked again next
        // frame: the flat state drops the DEM, and during the rise every read failed and the pair
        // kept was the flattened ZERO - the deck stayed on the water after the ground came back.
        bool retry = false;
        for (std::size_t recordIndex = 0; recordIndex < spanRecords.size(); recordIndex++) {
            const TileGeometry::SpanRecord& record = spanRecords[recordIndex];
            auto it = _spanUnions.find(SpanPieceKey { sourceTileId, record.featureId, record.vertexOffset, geometry->getType() });
            // A piece the cull did not chord - a stand-in tile, a retained one outside the set, a group
            // with no portal - borrows from the chord cache by its own ends here: a chord another zoom
            // group resolved later in the pass, or one from an earlier cull, is there by now.
            const SpanUnion* union_ = (it != _spanUnions.end() && it->second.have0 && it->second.have1) ? &it->second : nullptr;
            SpanUnion borrowed;
            // The chord this record stood on last time, first: its entry is still read every ramp
            // frame, which is what keeps a retained piece moving with the ground.
            if (!union_) {
                const TileGeometry::SpanChordRef& ref = geometry->getSpanRecordChord(recordIndex);
                if (ref.valid) {
                    borrowed.portal0 = ref.portal0;
                    borrowed.portal1 = ref.portal1;
                    borrowed.have0 = borrowed.have1 = true;
                    union_ = &borrowed;
                }
            }
            if (!union_) {
                cglib::vec2<double> e0 = cglib::transform_point(cglib::vec2<double>(record.p0(0), 1.0 - record.p0(1)), tileMatrix);
                cglib::vec2<double> e1 = cglib::transform_point(cglib::vec2<double>(record.p1(0), 1.0 - record.p1(1)), tileMatrix);
                auto chordIt = SpanGeometry::borrowChord(e0, record.portal0, e1, record.portal1, _spanChordCache.begin(), _spanChordCache.end(),
                                                         [](auto it) -> const CachedChord& { return it->second; });
                if (chordIt != _spanChordCache.end() && chordIt->second.haveHeights) {
                    borrowed.portal0 = chordIt->second.portal0;
                    borrowed.portal1 = chordIt->second.portal1;
                    borrowed.have0 = borrowed.have1 = true;
                    borrowed.height0 = chordIt->second.height0;
                    borrowed.height1 = chordIt->second.height1;
                    borrowed.haveHeights = true;
                    union_ = &borrowed;
                }
            }
            // Both portals or nothing: a chord to a tile CUT dives to whatever the ground does
            // there, which is worse than draping. Missing pieces resolve on a later frame.
            if (!union_) {
                if (!wasResolved) {
                    allResolved = false;
                    continue;
                }
                for (std::size_t i = record.vertexOffset; i < std::min(vertexCount, record.vertexOffset + record.vertexCount); i++) {
                    patched[i] = true;
                }
                continue;
            }
            const cglib::vec2<double>& w0 = union_->portal0;
            const cglib::vec2<double>& w1 = union_->portal1;
            geometry->setSpanRecordChord(recordIndex, w0, w1);
            // The ground AT the junction, where the approach road is drawn: that road is draped, so
            // anchoring the deck to the same value is what makes the two meet instead of stepping.
            // Usually resolved when the union was built, so this is the late arrival.
            double h0 = union_->height0, h1 = union_->height1;
            // The pair comes from the chord's cache entry, which outlives this piece's union and is
            // shared by every piece on the chord - the union's copy is only what the cull saw. Read
            // again when the elevation version moves, keeping the last pair if the read fails.
            auto chordIt = _spanChordCache.find(chordKey(w0, w1));
            bool haveChord = false;
            if (chordIt != _spanChordCache.end()) {
                if (chordIt->second.baseVersion != version && !sampleChordHeights(chordIt->second, sourceTileId.zoom)) {
                    retry = true;
                }
                haveChord = chordIt->second.haveHeights;
            }
            if (haveChord) {
                h0 = chordIt->second.height0;
                h1 = chordIt->second.height1;
                if (it != _spanUnions.end() && (!it->second.haveHeights || it->second.height0 != h0 || it->second.height1 != h1)) {
                    it->second.height0 = h0;
                    it->second.height1 = h1;
                    it->second.haveHeights = true;
                    rebuildSpanChords();
                }
            } else if (!union_->haveHeights) {
                allResolved = false;
                continue;
            }
            // The base from the vertex's own place along the chord - a deck is FLAT, whatever the
            // ground does at either side of its abutment - and that place itself, unclamped, for
            // the shader to tell the deck past the portals (TileGeometry::chordOffset).
            auto resolveVertex = [&](std::size_t i) {
                const std::uint8_t* vertex = vertexGeometry.data() + i * params.vertexSize;
                const std::int16_t* pos = reinterpret_cast<const std::int16_t*>(vertex + params.coordOffset);
                cglib::vec2<double> p(pos[0] / static_cast<double>(params.coordScale), pos[1] / static_cast<double>(params.coordScale));
                cglib::vec2<double> w = cglib::transform_point(cglib::vec2<double>(p(0), 1.0 - p(1)), tileMatrix);
                double t = SpanGeometry::chordParamRaw(w, w0, w1);
                double base = SpanGeometry::chordHeight(h0, h1, std::max(0.0, std::min(1.0, t)));
                geometry->setVertexBase(i, static_cast<float>(base + baseOffsetAt(w, record.baseOffset)));
                geometry->setVertexChord(i, static_cast<float>(t));
                patched[i] = true;
            };
            std::size_t last = std::min(vertexCount, record.vertexOffset + record.vertexCount);
            for (std::size_t i = record.vertexOffset; i < last; i++) {
                resolveVertex(i);
            }
            // ...and the same chord for whatever the records did not reach. One geometry holds one
            // structure here, so the first resolved chord is the right one for all of it.
            for (std::size_t i = 0; i < vertexCount; i++) {
                if (!patched[i]) {
                    resolveVertex(i);
                }
            }
        }
        if (!allResolved) {
            return false;
        }
        geometry->setBaseResolved(true);
        if (!retry) {
            geometry->setBaseElevationVersion(version);
            geometry->setBaseSpanVersion(spanVersion);
        }
        return true;
    }

}
