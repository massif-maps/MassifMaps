#include "TileDataSource.h"
#include "core/Variant.h"
#include "projections/Projection.h"
#include "projections/EPSG3857.h"
#include "utils/Const.h"

#include <algorithm>
#include <sstream>

#include <boost/lexical_cast.hpp>

namespace massif {

    TileDataSource::~TileDataSource() {
    }
        
    int TileDataSource::getMinZoom() const {
        return _minZoom;
    }
    
    int TileDataSource::getMaxZoom() const {
        return _maxZoom;
    }

    int TileDataSource::getMaxZoomWithOverzoom() const {
        if (_maxOverzoomLevel >= 0) {
            return getMaxZoom() + _maxOverzoomLevel;
        }
        return getMaxZoom();
    }

    int TileDataSource::getMaxOverzoomLevel() const {
        return _maxOverzoomLevel;
    }
    bool TileDataSource::isMaxOverzoomLevelSet() const {
        return _maxOverzoomLevel >= 0;
    }

    void TileDataSource::setMaxOverzoomLevel(int overzoomLevel) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _maxOverzoomLevel = overzoomLevel;
        }
        notifyTilesChanged(false);
    }

    MapBounds TileDataSource::getDataExtent() const {
        return _projection->getBounds();
    }
    
    std::shared_ptr<Projection> TileDataSource::getProjection() const {
        return _projection;
    }
    
    void TileDataSource::notifyTilesChanged(bool removeTiles) {
        std::vector<std::shared_ptr<OnChangeListener> > onChangeListeners;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            onChangeListeners = _onChangeListeners;
        }
        for (const std::shared_ptr<OnChangeListener>& listener : onChangeListeners) {
            listener->onTilesChanged(removeTiles);
        }
    }
        
    void TileDataSource::registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_mutex);
        _onChangeListeners.push_back(listener);
    }
    
    void TileDataSource::unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_mutex);
        _onChangeListeners.erase(std::remove(_onChangeListeners.begin(), _onChangeListeners.end(), listener), _onChangeListeners.end());
    }
    
    TileDataSource::TileDataSource() :
        _minZoom(0),
        _maxZoom(Const::MAX_SUPPORTED_ZOOM_LEVEL),
        _maxOverzoomLevel(-1),
        _projection(std::make_shared<EPSG3857>()),
        _metaData(),
        _onChangeListeners(),
        _mutex()
    {
    }

    TileDataSource::TileDataSource(int minZoom, int maxZoom) :
        _minZoom(std::max(0, minZoom)),
        _maxZoom(std::min(static_cast<int>(Const::MAX_SUPPORTED_ZOOM_LEVEL), maxZoom)),
        _maxOverzoomLevel(-1),
        _projection(std::make_shared<EPSG3857>()),
        _metaData(),
        _onChangeListeners(),
        _mutex()
    {
    }
    
    std::map<std::string, std::string> TileDataSource::buildTagValues(const MapTile& tile) const {
        std::map<std::string, std::string> tagValues;

        std::stringstream ss;
        for (int i = tile.getZoom() - 1; i >= 0; i--) {
            ss << (((tile.getY() >> i) & 1) * 2 + ((tile.getX() >> i) & 1));
        }
        tagValues["quadkey"] = ss.str();

        tagValues["x"] = boost::lexical_cast<std::string>(tile.getX());
        tagValues["xflipped"] = boost::lexical_cast<std::string>((1 << tile.getZoom()) - 1 - tile.getX());

        tagValues["y"] = boost::lexical_cast<std::string>(tile.getY());
        tagValues["yflipped"] = boost::lexical_cast<std::string>((1 << tile.getZoom()) - 1 - tile.getY());

        tagValues["z"] = boost::lexical_cast<std::string>(tile.getZoom());
        tagValues["zoom"] = boost::lexical_cast<std::string>(tile.getZoom());

        return tagValues;
    }
    
    std::shared_ptr<const std::map<std::string, Variant> > TileDataSource::getMetaDataPtr() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _metaData;
    }

    std::map<std::string, Variant> TileDataSource::getMetaData() const {
        std::shared_ptr<const std::map<std::string, Variant> > metaData = getMetaDataPtr();
        return metaData ? *metaData : std::map<std::string, Variant>();
    }

    void TileDataSource::setMetaData(const std::map<std::string, Variant>& metaData) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _metaData = metaData.empty() ? std::shared_ptr<const std::map<std::string, Variant> >() : std::make_shared<const std::map<std::string, Variant> >(metaData);
        }
        notifyTilesChanged(false);
    }

    bool TileDataSource::containsMetaDataKey(const std::string& key) const {
        return getMetaDataElement(key).getType() != VariantType::VARIANT_TYPE_NULL;
    }

    Variant TileDataSource::getMetaDataElement(const std::string& key) const {
        std::shared_ptr<const std::map<std::string, Variant> > metaData = getMetaDataPtr();
        if (metaData) {
            auto it = metaData->find(key);
            if (it != metaData->end()) {
                return it->second;
            }
        }
        // Fall back to the container's own metadata, so a tileset that already declares the key
        // (an MBTiles metadata table row, say) works without the application repeating it.
        std::string containerValue = getContainerMetaData(key);
        return containerValue.empty() ? Variant() : Variant(containerValue);
    }

    void TileDataSource::setMetaDataElement(const std::string& key, const Variant& element) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto metaData = _metaData ? std::make_shared<std::map<std::string, Variant> >(*_metaData) : std::make_shared<std::map<std::string, Variant> >();
            (*metaData)[key] = element;
            _metaData = metaData;
        }
        notifyTilesChanged(false);
    }

    void TileDataSource::applyTileMetaData(const std::shared_ptr<TileData>& tileData) const {
        if (!tileData) {
            return;
        }
        tileData->setMetaData(getMetaDataPtr());
    }

    std::string TileDataSource::getContainerMetaData(const std::string& key) const {
        return std::string();
    }

}

