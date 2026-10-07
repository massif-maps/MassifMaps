/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_POINTGRIDMEMO_H_
#define _MASSIF_POINTGRIDMEMO_H_

#include "core/MapPos.h"
#include "core/MapTile.h"

#include <memory>

namespace massif {

    /**
     * The last grid a point lookup resolved to, reused for the next point it contains. Only a grid that IS the
     * looked-up tile is kept: an ancestor standing in for one point is not the answer for a neighbouring point
     * whose own grid is cached, and reusing it flipped the terrain focus between the two levels.
     */
    template <typename Grid, typename Mode>
    class PointGridMemo {
    public:
        std::shared_ptr<Grid> find(unsigned long long owner, unsigned int version, Mode mode, double x, double y) const {
            if (_grid && _owner == owner && _version == version && _mode == mode && _grid->getInternalBounds().contains(MapPos(x, y, 0))) {
                return _grid;
            }
            return std::shared_ptr<Grid>();
        }

        void remember(unsigned long long owner, unsigned int version, Mode mode, const std::shared_ptr<Grid>& grid, const MapTile& tile) {
            if (!grid || grid->getTile().getTileId() != tile.getTileId()) {
                return;
            }
            _owner = owner;
            _version = version;
            _mode = mode;
            _grid = grid;
        }

    private:
        unsigned long long _owner = 0;
        unsigned int _version = 0;
        Mode _mode = Mode();
        std::shared_ptr<Grid> _grid;
    };

}

#endif
