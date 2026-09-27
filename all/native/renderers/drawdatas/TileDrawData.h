/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TILEDRAWDATA_H_
#define _MASSIF_TILEDRAWDATA_H_

#include <memory>

#include <vt/TileId.h>

namespace massif {
    namespace vt {
        class Tile;
    }

    class TileDrawData {
    public:
        TileDrawData(const vt::TileId& vtTileId, const std::shared_ptr<const vt::Tile>& vtTile, long long tileId, bool preloadingTile, bool shadowCasterTile = false);
        virtual ~TileDrawData();

        const vt::TileId& getVTTileId() const;
        const std::shared_ptr<const vt::Tile>& getVTTile() const;
        
        bool isPreloadingTile() const;
        // Past the view on the sun's side: its extrusions cast into the view, nothing of it is drawn.
        bool isShadowCasterTile() const;
        long long getTileId() const;
    
    private:
        vt::TileId _vtTileId;
        std::shared_ptr<const vt::Tile> _vtTile;

        long long _tileId;
        bool _preloadingTile;
        bool _shadowCasterTile;
    };
    
}

#endif
