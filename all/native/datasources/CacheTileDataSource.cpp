#include "CacheTileDataSource.h"
#include "core/MapTile.h"
#include "core/Variant.h"
#include "datasources/components/TileData.h"
#include "components/Exceptions.h"
#include "utils/Log.h"

#include <memory>

namespace massif {
    
    CacheTileDataSource::CacheTileDataSource(const std::shared_ptr<TileDataSource>& dataSource) :
        TileDataSource(),
        _dataSource(dataSource)
    {
        if (!dataSource) {
            throw NullArgumentException("Null dataSource");
        }

        _dataSourceListener = std::make_shared<DataSourceListener>(*this);
        _dataSource->registerOnChangeListener(_dataSourceListener);
    }
    
    CacheTileDataSource::~CacheTileDataSource() {
        _dataSource->unregisterOnChangeListener(_dataSourceListener);
        _dataSourceListener.reset();
    }

    int CacheTileDataSource::getMinZoom() const {
        return _dataSource->getMinZoom();
    }

    int CacheTileDataSource::getMaxZoom() const {
        return _dataSource->getMaxZoom();
    }

    MapBounds CacheTileDataSource::getDataExtent() const {
        return _dataSource->getDataExtent();
    }

    std::shared_ptr<const std::map<std::string, Variant> > CacheTileDataSource::getMetaDataPtr() const {
        std::shared_ptr<const std::map<std::string, Variant> > metaData = TileDataSource::getMetaDataPtr();
        return metaData ? metaData : _dataSource->getMetaDataPtr();
    }

    std::string CacheTileDataSource::getContainerMetaData(const std::string& key) const {
        return _dataSource->getContainerMetaData(key);
    }

    void CacheTileDataSource::applyCacheTileMetaData(const std::shared_ptr<TileData>& tileData) const {
        if (!tileData) {
            return;
        }

        std::shared_ptr<const std::map<std::string, Variant> > own = TileDataSource::getMetaDataPtr();
        if (!own) {
            // Only fill in for a tile that carries nothing - a tile from a wrapped OrderedTileDataSource
            // already carries the LEAF's map, which is the only place its real settings are known.
            if (!tileData->getMetaData()) {
                tileData->setMetaData(_dataSource->getMetaDataPtr());
            }
            return;
        }
        for (const auto& entry : *own) {
            tileData->setMetaDataElement(entry.first, entry.second);
        }
    }

    void CacheTileDataSource::notifyTilesChanged(bool removeTiles) {
        clear();
        TileDataSource::notifyTilesChanged(removeTiles);
    }

    std::shared_ptr<TileDataSource> CacheTileDataSource::getDataSource() const {
        return _dataSource.get();
    }
    
    CacheTileDataSource::DataSourceListener::DataSourceListener(CacheTileDataSource& cacheDataSource) :
        _cacheDataSource(cacheDataSource)
    {
    }
    
    void CacheTileDataSource::DataSourceListener::onTilesChanged(bool removeTiles) {
        _cacheDataSource.notifyTilesChanged(removeTiles);
    }

}
