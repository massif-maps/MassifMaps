#include "TileDrawData.h"

#include <vt/Tile.h>

namespace massif {

    TileDrawData::TileDrawData(const vt::TileId& vtTileId, const std::shared_ptr<const vt::Tile>& vtTile, long long tileId, bool preloadingTile, bool shadowCasterTile) :
        _vtTileId(vtTileId),
        _vtTile(vtTile),
        _tileId(tileId),
        _preloadingTile(preloadingTile),
        _shadowCasterTile(shadowCasterTile)
    {
    }
    
    TileDrawData::~TileDrawData() {
    }

    const vt::TileId& TileDrawData::getVTTileId() const {
         return _vtTileId;
    }
        
    const std::shared_ptr<const vt::Tile>& TileDrawData::getVTTile() const {
         return _vtTile;
    }
        
    bool TileDrawData::isPreloadingTile() const {
        return _preloadingTile;
    }

    bool TileDrawData::isShadowCasterTile() const {
        return _shadowCasterTile;
    }
    
    long long TileDrawData::getTileId() const {
        return _tileId;
    }
    
}
