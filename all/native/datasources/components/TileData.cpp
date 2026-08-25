#include "TileData.h"
#include "core/BinaryData.h"
#include "core/Variant.h"

namespace massif {
    
    TileData::TileData(const std::shared_ptr<BinaryData>& data) :
        _data(data), _width(0), _height(0), _expirationTime(), _replaceWithParent(false), _overzoom(false), _metaData(), _mutex()
    {
    }

    TileData::TileData(const std::shared_ptr<BinaryData>& pixels, int width, int height) :
        _data(pixels), _width(width), _height(height), _expirationTime(), _replaceWithParent(false), _overzoom(false), _metaData(), _mutex()
    {
    }

    TileData::~TileData() {
    }

    long long TileData::getMaxAge() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_expirationTime) {
            return -1;
        } else {
            long long maxAge = std::chrono::duration_cast<std::chrono::milliseconds>(*_expirationTime - std::chrono::steady_clock::now()).count();
            return maxAge > 0 ? maxAge : 0;
        }
    }

    void TileData::setMaxAge(long long maxAge) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (maxAge < 0) {
            _expirationTime.reset();
        } else {
            _expirationTime = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now() + std::chrono::milliseconds(maxAge));
        }
    }
    
    bool TileData::isReplaceWithParent() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _replaceWithParent;
    }
    
    void TileData::setReplaceWithParent(bool flag) {
        std::lock_guard<std::mutex> lock(_mutex);
        _replaceWithParent = flag;
    }
    
    bool TileData::isOverZoom() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _overzoom;
    }
    
    void TileData::setIsOverZoom(bool flag) {
        std::lock_guard<std::mutex> lock(_mutex);
        _overzoom = flag;
    }
    
    const std::shared_ptr<BinaryData>& TileData::getData() const {
        return _data;
    }

    bool TileData::isRawPixels() const {
        return _width > 0 && _height > 0;
    }

    int TileData::getWidth() const {
        return _width;
    }

    int TileData::getHeight() const {
        return _height;
    }
    
    std::shared_ptr<const std::map<std::string, Variant> > TileData::getMetaData() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _metaData;
    }

    void TileData::setMetaData(const std::shared_ptr<const std::map<std::string, Variant> >& metaData) {
        std::lock_guard<std::mutex> lock(_mutex);
        _metaData = metaData;
    }

    bool TileData::containsMetaDataKey(const std::string& key) const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _metaData && _metaData->find(key) != _metaData->end();
    }

    Variant TileData::getMetaDataElement(const std::string& key) const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_metaData) {
            auto it = _metaData->find(key);
            if (it != _metaData->end()) {
                return it->second;
            }
        }
        return Variant();
    }

    void TileData::setMetaDataElement(const std::string& key, const Variant& element) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto metaData = _metaData ? std::make_shared<std::map<std::string, Variant> >(*_metaData) : std::make_shared<std::map<std::string, Variant> >();
        (*metaData)[key] = element;
        _metaData = metaData;
    }

}
