#include "HTTPTileDataSource.h"
#include "core/MapTile.h"
#include "core/BinaryData.h"
#include "components/Exceptions.h"
#include "datasources/components/PMTilesUtils.h"
#include "utils/Log.h"
#include "utils/NetworkUtils.h"
#include "utils/GeneralUtils.h"

#include <cinttypes>
#include <algorithm>

namespace massif {

    HTTPTileDataSource::HTTPTileDataSource(int minZoom, int maxZoom, const std::string& baseURL) :
        TileDataSource(minZoom, maxZoom),
        _baseURL(baseURL),
        _subdomains({ "a", "b", "c", "d" }),
        _tmsScheme(false),
        _maxAgeHeaderCheck(false),
        _timeout(-1),
        _headers(),
        _httpClient(true),
        _randomGenerator(),
        _mutex(),
        _pmtilesCache()
    {
    }
    
    HTTPTileDataSource::~HTTPTileDataSource() {
    }
    
    std::string HTTPTileDataSource::getBaseURL() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _baseURL;
    }
    
    void HTTPTileDataSource::setBaseURL(const std::string& baseURL) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _baseURL = baseURL;
            // Clear PMTiles cache when URL changes
            _pmtilesCache.reset();
        }
        notifyTilesChanged(false);
    }
    
    std::vector<std::string> HTTPTileDataSource::getSubdomains() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _subdomains;
    }
    
    void HTTPTileDataSource::setSubdomains(const std::vector<std::string>& subdomains) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _subdomains = subdomains;
        }
        notifyTilesChanged(false);
    }
    
    bool HTTPTileDataSource::isTMSScheme() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tmsScheme;
    }
    
    void HTTPTileDataSource::setTMSScheme(bool tmsScheme) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _tmsScheme = tmsScheme;
        }
        notifyTilesChanged(false);
    }
    
    bool HTTPTileDataSource::isMaxAgeHeaderCheck() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _maxAgeHeaderCheck;
    }
    
    void HTTPTileDataSource::setMaxAgeHeaderCheck(bool maxAgeCheck) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _maxAgeHeaderCheck = maxAgeCheck;
        }
        notifyTilesChanged(false);
    }
    
    int HTTPTileDataSource::getTimeout() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _timeout;
    }

    void HTTPTileDataSource::setTimeout(int timeout) {
        std::lock_guard<std::mutex> lock(_mutex);
        _httpClient.setTimeout(timeout < 0 ? -1 : timeout * 1000);
        _timeout = timeout;
    }

    std::map<std::string, std::string> HTTPTileDataSource::getHTTPHeaders() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _headers;
    }
    
    void HTTPTileDataSource::setHTTPHeaders(const std::map<std::string, std::string>& headers) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _headers = headers;
        }
        notifyTilesChanged(false);
    }
    
    std::shared_ptr<TileData> HTTPTileDataSource::loadTile(const MapTile& mapTile) {
        std::string baseURL;
        std::map<std::string, std::string> headers;
        bool maxAgeHeaderCheck;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            baseURL = _baseURL;
            headers = _headers;
            maxAgeHeaderCheck = _maxAgeHeaderCheck;
        }

        std::shared_ptr<TileData> tileData;
        // Check if this is a PMTiles URL
        if (isPMTilesURL(baseURL)) {
            auto tileData = loadPMTile(baseURL, mapTile);
            applyTileMetaData(tileData);
            return tileData;
        } else {

            std::string url = buildTileURL(baseURL, mapTile);
            if (url.empty()) {
                return std::shared_ptr<TileData>();
            }

            Log::Infof("HTTPTileDataSource::loadTile: Loading %s", url.c_str());
            std::map<std::string, std::string> responseHeaders;
            std::shared_ptr<BinaryData> responseData;
            try {
                if (_httpClient.get(url, headers, responseHeaders, responseData) != 0) {
                    Log::Errorf("HTTPTileDataSource::loadTile: Failed to load %s", url.c_str());
                    return std::shared_ptr<TileData>();
                }
            }
            catch (const std::exception& ex) {
                Log::Errorf("HTTPTileDataSource::loadTile: Exception while loading tile %d/%d/%d: %s", mapTile.getZoom(), mapTile.getX(), mapTile.getY(), ex.what());
                return std::shared_ptr<TileData>();
            }
            auto tileData = std::make_shared<TileData>(responseData);
            if (maxAgeHeaderCheck) {
                int maxAge = NetworkUtils::GetMaxAgeHTTPHeader(responseHeaders);
                if (maxAge >= 0) {
                    tileData->setMaxAge(maxAge * 1000);
                }
            }
            applyTileMetaData(tileData);
            return tileData;
        }
    }
    
    std::string HTTPTileDataSource::buildTileURL(const std::string& baseURL, const MapTile& tile) const {
        bool tmsScheme = false;
        std::string subdomain;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            tmsScheme = _tmsScheme;
            if (!_subdomains.empty()) {
                std::size_t randomIndex = std::uniform_int_distribution<std::size_t>(0, _subdomains.size() - 1)(_randomGenerator);
                subdomain = _subdomains[randomIndex];
            }
        }

        std::map<std::string, std::string> tagValues = buildTagValues(tmsScheme ? tile.getFlipped() : tile);
        if (!subdomain.empty()) {
            tagValues["s"] = subdomain;
        }
   
        return GeneralUtils::ReplaceTags(baseURL, tagValues, "{", "}", true);
    }
    
    // PMTiles support implementation
    
    bool HTTPTileDataSource::isPMTilesURL(const std::string& url) const {
        // Check if URL starts with "pmtiles://"
        if (url.size() >= 11 && url.substr(0, 11) == "pmtiles://") {
            return true;
        }
        
        // Check if URL ends with ".pmtiles"
        if (url.size() >= 8) {
            std::string extension = url.substr(url.size() - 8);
            if (extension == ".pmtiles") {
                return true;
            }
        }
        
        return false;
    }
    
    std::string HTTPTileDataSource::normalizePMTilesURL(const std::string& url) const {
        // If URL starts with "pmtiles://", replace with "https://"
        if (url.size() >= 11 && url.substr(0, 11) == "pmtiles://") {
            return "https://" + url.substr(11);
        }
        return url;
    }
    
    std::shared_ptr<TileData> HTTPTileDataSource::loadPMTile(const std::string& baseURL, const MapTile& mapTile) {
        try {
            std::string url = normalizePMTilesURL(baseURL);
            
            // Initialize PMTiles cache if needed (with mutex protection)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                if (!_pmtilesCache || _pmtilesCache->url != url) {
                    Log::Infof("HTTPTileDataSource::loadPMTile: Initializing PMTiles archive from %s", url.c_str());
                    
                    // Create a new cache object
                    auto newCache = std::make_unique<PMTilesCache>();
                    newCache->url = url;
                    
                    // Unlock during HTTP requests to allow parallel tile fetches
                    lock.unlock();
                    
                    // Read header
                    newCache->header = readPMTilesHeader(url);
                    
                    // Read and decode root directory
                    std::vector<uint8_t> rootDirData = httpRangeRequest(url, newCache->header.rootDirectoryOffset, newCache->header.rootDirectoryLength);
                    std::vector<uint8_t> decompressed = pmtiles::decompressData(rootDirData, newCache->header.internalCompression);
                    newCache->rootDirectory = pmtiles::decodeDirectory(decompressed);
                    
                    // Re-lock and update cache atomically
                    lock.lock();
                    // Check again in case another thread initialized it while we were unlocked
                    if (!_pmtilesCache || _pmtilesCache->url != url) {
                        _pmtilesCache = std::move(newCache);
                        Log::Infof("HTTPTileDataSource::loadPMTile: PMTiles header loaded, tiles: %" PRIu64 ", zoom: %d-%d", 
                                   _pmtilesCache->header.numTileEntries, _pmtilesCache->header.minZoom, _pmtilesCache->header.maxZoom);
                    }
                }
            }
            
            // Convert tile coordinates to PMTiles TileID
            uint64_t tileId = pmtiles::zxyToTileId(mapTile.getZoom(), mapTile.getX(), mapTile.getY());
            
            // Search for tile in cache (with mutex protection)
            pmtiles::DirectoryEntry entry;
            bool found = false;
            pmtiles::Header header;
            
            {
                std::unique_lock<std::mutex> lock(_mutex);
                
                // Search for tile in root directory
                found = pmtiles::findTileEntry(_pmtilesCache->rootDirectory, tileId, entry);
                
                // If not found in root, search in leaf directories
                if (!found) {
                    // Binary search in root directory to find the leaf directory
                    auto it = std::lower_bound(_pmtilesCache->rootDirectory.begin(), _pmtilesCache->rootDirectory.end(), tileId,
                        [](const pmtiles::DirectoryEntry& entry, uint64_t id) { return entry.tileId < id; });
                    
                    if (it != _pmtilesCache->rootDirectory.begin()) {
                        --it;
                        
                        // Check if this points to a leaf directory
                        if (it->runLength == 0) {
                            // Check if leaf directory is cached
                            uint64_t leafKey = it->offset;
                            auto leafIt = _pmtilesCache->leafDirectoryCache.find(leafKey);
                            if (leafIt == _pmtilesCache->leafDirectoryCache.end()) {
                                // Need to load leaf directory - unlock during HTTP request
                                uint64_t leafOffset = it->offset;
                                uint32_t leafLength = it->length;
                                header = _pmtilesCache->header;
                                
                                lock.unlock();
                                std::vector<pmtiles::DirectoryEntry> leafDir = loadPMTilesLeafDirectory(url, header, leafOffset, leafLength);
                                lock.lock();

                                // Insert into cache (might already be there if another thread loaded it)
                                leafIt = _pmtilesCache->leafDirectoryCache.insert({leafKey, std::move(leafDir)}).first;
                            }
                            
                            found = pmtiles::findTileEntry(leafIt->second, tileId, entry);
                        }
                    }
                }
                
                // Copy header for use outside the lock
                header = _pmtilesCache->header;
            }
            
            if (!found) {
                // Tile not found, try parent tile
                if (mapTile.getZoom() > getMinZoom()) {
                    Log::Infof("HTTPTileDataSource::loadPMTile: Tile not found, redirecting to parent");
                    auto tileData = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
                    tileData->setReplaceWithParent(true);
                    return tileData;
                } else {
                    Log::Infof("HTTPTileDataSource::loadPMTile: Tile not found");
                    return std::shared_ptr<TileData>();
                }
            }
            
            // Read tile data (without mutex - independent HTTP request)
            std::vector<uint8_t> compressedData = httpRangeRequest(url, 
                header.tileDataOffset + entry.offset, entry.length);
            
            // Decompress if needed
            std::vector<uint8_t> tileBytes = pmtiles::decompressData(compressedData, header.tileCompression);
            
            auto data = std::make_shared<BinaryData>(tileBytes.data(), tileBytes.size());
            return std::make_shared<TileData>(data);
        }
        catch (const std::exception& ex) {
            Log::Errorf("HTTPTileDataSource::loadPMTile: Failed to load tile: %s", ex.what());
            return std::shared_ptr<TileData>();
        }
    }
    
    pmtiles::Header HTTPTileDataSource::readPMTilesHeader(const std::string& url) {
        // Read 127-byte header
        std::vector<uint8_t> headerBytes = httpRangeRequest(url, 0, 127);
        
        if (headerBytes.size() != 127) {
            throw GenericException("Failed to read PMTiles header");
        }
        
        return pmtiles::readHeader(headerBytes.data());
    }
    
    std::vector<uint8_t> HTTPTileDataSource::httpRangeRequest(const std::string& url, uint64_t offset, uint64_t length) {
        // Use a custom handler that collects the data
        std::vector<uint8_t> result;
        result.reserve(static_cast<size_t>(length));
        
        auto handlerFn = [&result, offset, length](std::uint64_t dataOffset, std::uint64_t totalLength, const unsigned char* buf, std::size_t size) -> bool {
            result.insert(result.end(), buf, buf + size);
            return true;
        };
        
        // Build request headers with Range
        std::map<std::string, std::string> requestHeaders = _headers;
        requestHeaders["Range"] = "bytes=" + std::to_string(offset) + "-" + std::to_string(offset + length - 1);
        
        std::map<std::string, std::string> responseHeaders;
        
        int errorCode = _httpClient.streamResponse("GET", url, requestHeaders, responseHeaders, handlerFn, offset);
        if (errorCode != 0) {
            throw GenericException("HTTP range request failed");
        }
        
        if (result.empty()) {
            throw GenericException("Empty HTTP range response");
        }
        
        // Verify we got the expected amount of data
        if (result.size() != length) {
            Log::Warnf("HTTPTileDataSource::httpRangeRequest: Expected %" PRIu64 " bytes but got %zu bytes for range %" PRIu64 "-%" PRIu64 " from %s",
                length, result.size(), offset, offset + length - 1, url.c_str());
        }
        
        return result;
    }
    
    std::vector<pmtiles::DirectoryEntry> HTTPTileDataSource::loadPMTilesLeafDirectory(const std::string& url, const pmtiles::Header& header, uint64_t offset, uint32_t length) {
        std::vector<uint8_t> leafDirData = httpRangeRequest(url, header.leafDirectoriesOffset + offset, length);
        std::vector<uint8_t> decompressed = pmtiles::decompressData(leafDirData, header.internalCompression);
        return pmtiles::decodeDirectory(decompressed);
    }
    
}

