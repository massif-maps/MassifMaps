/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINDECODEWAIT_H_
#define _MASSIF_TERRAINDECODEWAIT_H_

#include <iterator>
#include <unordered_set>

namespace massif {

    /**
     * What a 2D/3D switch waits for on one layer: only the tiles its decode swap invalidated, since a
     * moving camera always has something fetching. See docs/internals/rendering/04-terrain.md.
     * TileLayer holds one behind its own mutex.
     */
    class TerrainDecodeWait {
    public:
        bool isSettled() const { return _settled; }

        /** How many tiles still owe the switch a decode; -1 before the cull that names them. */
        int getPendingCount() const {
            return _settled ? 0 : (_pending ? -1 : static_cast<int>(_tiles.size()));
        }

        /** A decode swap just invalidated the visible tiles. What it invalidated is not known yet. */
        void markUnsettled() {
            _settled = false;
            _pending = true;
            _tiles.clear();
        }

        /** The first cull after the swap names the visible tiles it had to refetch. */
        void recordFetched(const std::unordered_set<long long>& visibleTiles) {
            if (!_pending) {
                return;
            }
            _pending = false;
            _tiles = visibleTiles;
        }

        /** Nothing of this layer is on screen, so the switch has nothing to wait for here. */
        void settleNow() {
            _pending = false;
            _settled = true;
            _tiles.clear();
        }

        /** isFetching(tileId) is true while that tile still owes the switch a decode. */
        template <typename IsFetching>
        bool settle(IsFetching isFetching) {
            if (_settled) {
                return true;
            }
            if (_pending) {
                return false;
            }
            for (auto it = _tiles.begin(); it != _tiles.end(); ) {
                it = isFetching(*it) ? std::next(it) : _tiles.erase(it);
            }
            if (!_tiles.empty()) {
                return false;
            }
            _settled = true;
            return true;
        }

    private:
        bool _settled = true;
        bool _pending = false;
        std::unordered_set<long long> _tiles;
    };

}

#endif
