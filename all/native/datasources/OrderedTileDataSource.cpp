#include "OrderedTileDataSource.h"
#include "core/MapTile.h"
#include "core/Variant.h"
#include "components/Exceptions.h"
#include "utils/Log.h"

#include <memory>
#include <algorithm>

namespace massif {
    
    OrderedTileDataSource::OrderedTileDataSource(const std::shared_ptr<TileDataSource>& dataSource1, const std::shared_ptr<TileDataSource>& dataSource2) :
        TileDataSource(),
        _dataSource1(dataSource1),
        _dataSource2(dataSource2)
    {
        if (!dataSource1) {
            throw NullArgumentException("Null dataSource1");
        }
        if (!dataSource2) {
            throw NullArgumentException("Null dataSource2");
        }

        _dataSourceListener = std::make_shared<DataSourceListener>(*this);
        _dataSource1->registerOnChangeListener(_dataSourceListener);
        _dataSource2->registerOnChangeListener(_dataSourceListener);
    }
    
    OrderedTileDataSource::~OrderedTileDataSource() {
        _dataSource2->unregisterOnChangeListener(_dataSourceListener);
        _dataSource1->unregisterOnChangeListener(_dataSourceListener);
        _dataSourceListener.reset();
    }

   int OrderedTileDataSource::getMinZoom() const {
        return std::min(_dataSource1->getMinZoom(), _dataSource2->getMinZoom());
    }

    int OrderedTileDataSource::getMaxZoom() const {
        return std::max(_dataSource1->getMaxZoom(), _dataSource2->getMaxZoom());
    }

    MapBounds OrderedTileDataSource::getDataExtent() const {
        MapBounds bounds = _dataSource1->getDataExtent();
        bounds.expandToContain(_dataSource2->getDataExtent());
        return bounds;
    }

    std::shared_ptr<const std::map<std::string, Variant> > OrderedTileDataSource::getMetaDataPtr() const {
        std::shared_ptr<const std::map<std::string, Variant> > metaData = TileDataSource::getMetaDataPtr();
        if (metaData) {
            return metaData;
        }
        metaData = _dataSource1->getMetaDataPtr();
        return metaData ? metaData : _dataSource2->getMetaDataPtr();
    }

    std::string OrderedTileDataSource::getContainerMetaData(const std::string& key) const {
        std::string value = _dataSource1->getContainerMetaData(key);
        return value.empty() ? _dataSource2->getContainerMetaData(key) : value;
    }
    
    std::shared_ptr<TileData> OrderedTileDataSource::loadTile(const MapTile& mapTile) {
        std::shared_ptr<TileData> result1, result2;
        int zoom = mapTile.getZoom();
        if (zoom >= _dataSource1->getMinZoom()) {
            if (zoom <= _dataSource1->getMaxZoom()) {
                result1 = _dataSource1->loadTile(mapTile);
                if (result1 && !result1->isReplaceWithParent()) {
                    return result1;
                }
            } else {
                result1 = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
                result1->setReplaceWithParent(true);
            }
        }
        if (zoom >= _dataSource2->getMinZoom()) {
            if (zoom <= _dataSource2->getMaxZoom()) {
                result2 = _dataSource2->loadTile(mapTile);
                if (result2 && !result2->isReplaceWithParent()) {
                    return result2;
                }
            } else {
                result2 = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
                result2->setReplaceWithParent(true);
            }
        }
        return result1 ? result1 : result2;
    }

    OrderedTileDataSource::DataSourceListener::DataSourceListener(OrderedTileDataSource& combinedDataSource) :
        _combinedDataSource(combinedDataSource)
    {
    }
    
    void OrderedTileDataSource::DataSourceListener::onTilesChanged(bool removeTiles) {
        _combinedDataSource.notifyTilesChanged(removeTiles);
    }
    
}
