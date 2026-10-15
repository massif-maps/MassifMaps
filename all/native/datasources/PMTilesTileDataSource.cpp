#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64 // pread offsets past 2 GB on 32-bit ABIs
#endif

#ifdef _MASSIF_OFFLINE_SUPPORT

#include "PMTilesTileDataSource.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "components/Exceptions.h"
#include "datasources/components/PMTilesUtils.h"
#include "projections/Projection.h"
#include "utils/Log.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
// For JSON parsing (simple extraction)
#include <boost/algorithm/string.hpp>

namespace massif {

    PMTilesTileDataSource::PMTilesTileDataSource(const std::string& path) :
        TileDataSource(),
        _path(path),
        _header(),
        _rootDirectory(),
        _cachedMetadata(),
        _cachedDataExtent()
    {
        openArchive();
    }

    PMTilesTileDataSource::PMTilesTileDataSource(int minZoom, int maxZoom, const std::string& path) :
        TileDataSource(minZoom, maxZoom),
        _path(path),
        _header(),
        _rootDirectory(),
        _cachedMetadata(),
        _cachedDataExtent()
    {
        openArchive();
    }

    PMTilesTileDataSource::~PMTilesTileDataSource() {
#ifndef _WIN32
        if (_fd >= 0) {
            ::close(_fd);
        }
#endif
    }

    void PMTilesTileDataSource::openArchive() {
#ifdef _WIN32
        _file.open(_path, std::ios::binary);
        if (!_file.is_open()) {
            throw FileException("Failed to open PMTiles file", _path);
        }
#else
        _fd = ::open(_path.c_str(), O_RDONLY);
        if (_fd < 0) {
            throw FileException("Failed to open PMTiles file", _path);
        }
#endif

        try {
            std::vector<uint8_t> headerBytes = readRange(0, 127);
            _header = pmtiles::readHeader(headerBytes.data());

            std::vector<uint8_t> rootDirData = readRange(_header.rootDirectoryOffset, _header.rootDirectoryLength);
            _rootDirectory = pmtiles::decodeDirectory(pmtiles::decompressData(std::move(rootDirData), _header.internalCompression));
        }
        catch (...) {
#ifndef _WIN32
            ::close(_fd);
            _fd = -1;
#endif
            throw;
        }
        adoptContainerDemEncoding();

        Log::Infof("PMTilesTileDataSource: Opened %s with %llu tiles, zoom %d-%d",
                   _path.c_str(), _header.numTileEntries, _header.minZoom, _header.maxZoom);
    }

    std::vector<uint8_t> PMTilesTileDataSource::readRange(uint64_t offset, uint64_t length) const {
        std::vector<uint8_t> data(length);
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(_fileMutex);
        _file.clear();
        _file.seekg(offset);
        _file.read(reinterpret_cast<char*>(data.data()), length);
        if (!_file) {
            throw GenericException("Failed to read PMTiles data");
        }
#else
        // pread keeps no file position, so concurrent tile reads need no lock
        uint64_t done = 0;
        while (done < length) {
            ssize_t count = ::pread(_fd, data.data() + done, length - done, static_cast<off_t>(offset + done));
            if (count <= 0) {
                throw GenericException("Failed to read PMTiles data");
            }
            done += static_cast<uint64_t>(count);
        }
#endif
        return data;
    }

    std::string PMTilesTileDataSource::getContainerMetaData() const {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        
        if (!_cachedMetadata) {
            if (_header.metadataLength == 0) {
                _cachedMetadata = "";
                return *_cachedMetadata;
            }
            
            try {
                // Read metadata
                std::vector<uint8_t> metadataData = readRange(_header.metadataOffset, _header.metadataLength);

                // Decompress if needed
                std::vector<uint8_t> decompressed = pmtiles::decompressData(std::move(metadataData), _header.internalCompression);
                
                _cachedMetadata = std::string(decompressed.begin(), decompressed.end());
            }
            catch (const std::exception& ex) {
                Log::Errorf("PMTilesTileDataSource::getContainerMetaData: Failed to read metadata: %s", ex.what());
                _cachedMetadata = "";
            }
        }
        
        return *_cachedMetadata;
    }

    std::string PMTilesTileDataSource::getContainerMetaData(const std::string& key) const {
        std::string metadata = getContainerMetaData();
        if (metadata.empty()) {
            return "";
        }
        
        // Simple JSON key extraction (not a full JSON parser)
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = metadata.find(searchKey);
        if (keyPos == std::string::npos) {
            return "";
        }
        
        // Find the colon after the key
        size_t colonPos = metadata.find(':', keyPos);
        if (colonPos == std::string::npos) {
            return "";
        }
        
        // Skip whitespace after colon
        size_t valueStart = metadata.find_first_not_of(" \t\n\r", colonPos + 1);
        if (valueStart == std::string::npos) {
            return "";
        }
        
        // Extract value (handle strings and non-strings)
        if (metadata[valueStart] == '"') {
            // String value
            size_t valueEnd = metadata.find('"', valueStart + 1);
            if (valueEnd != std::string::npos) {
                return metadata.substr(valueStart + 1, valueEnd - valueStart - 1);
            }
        } else {
            // Non-string value (number, boolean, etc.)
            size_t valueEnd = metadata.find_first_of(",}", valueStart);
            if (valueEnd != std::string::npos) {
                std::string value = metadata.substr(valueStart, valueEnd - valueStart);
                boost::trim(value);
                return value;
            }
        }
        
        return "";
    }

    int PMTilesTileDataSource::getMinZoom() const {
        return _header.minZoom;
    }
    
    int PMTilesTileDataSource::getMaxZoom() const {
        return _header.maxZoom;
    }
    
    MapBounds PMTilesTileDataSource::getDataExtent() const {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        
        if (!_cachedDataExtent) {
            // Convert WGS84 bounds to projection bounds
            MapPos minPos = _projection->fromWgs84(MapPos(_header.minLon, _header.minLat));
            MapPos maxPos = _projection->fromWgs84(MapPos(_header.maxLon, _header.maxLat));
            _cachedDataExtent = MapBounds(minPos, maxPos);
        }
        
        return *_cachedDataExtent;
    }

    std::shared_ptr<TileData> PMTilesTileDataSource::loadTile(const MapTile& mapTile) {
        Log::Infof("PMTilesTileDataSource::loadTile: Loading %s", mapTile.toString().c_str());

        // Handle overzoom
        if (getMaxOverzoomLevel() >= 0 && mapTile.getZoom() > getMaxZoomWithOverzoom()) {
            auto tileData = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
            tileData->setIsOverZoom(true);
            return tileData;
        }
        
        try {
            // Convert tile coordinates to TileID using Hilbert curve
            uint64_t tileId = pmtiles::zxyToTileId(mapTile.getZoom(), mapTile.getX(), mapTile.getY());
            
            // Find the tile entry
            pmtiles::DirectoryEntry entry;
            if (!findTileEntry(tileId, entry)) {
                // Tile not found, try parent tile
                if (mapTile.getZoom() > getMinZoom()) {
                    Log::Infof("PMTilesTileDataSource::loadTile: Tile not found, redirecting to parent");
                    std::shared_ptr<TileData> tileData = std::make_shared<TileData>(std::shared_ptr<BinaryData>());
                    tileData->setReplaceWithParent(true);
                    return tileData;
                } else {
                    Log::Infof("PMTilesTileDataSource::loadTile: Tile not found");
                    return std::shared_ptr<TileData>();
                }
            }
            
            // Read tile data
            std::vector<uint8_t> compressedData = readRange(_header.tileDataOffset + entry.offset, entry.length);

            auto data = std::make_shared<BinaryData>(pmtiles::decompressData(std::move(compressedData), _header.tileCompression));
            auto tileData = std::make_shared<TileData>(data);
            applyTileMetaData(tileData);
            return tileData;
        }
        catch (const std::exception& ex) {
            Log::Errorf("PMTilesTileDataSource::loadTile: Failed to load tile: %s", ex.what());
            return std::shared_ptr<TileData>();
        }
    }

    bool PMTilesTileDataSource::findTileEntry(uint64_t tileId, pmtiles::DirectoryEntry& outEntry) {
        // Root, then at most a few levels of leaf directories (the spec allows 3)
        const Directory* directory = &_rootDirectory;
        std::shared_ptr<const Directory> leaf;
        for (int depth = 0; depth < 4; depth++) {
            const pmtiles::DirectoryEntry* entry = pmtiles::findDirectoryEntry(*directory, tileId);
            if (!entry) {
                return false;
            }
            if (entry->runLength > 0) {
                outEntry = *entry;
                return true;
            }
            try {
                leaf = getLeafDirectory(entry->offset, entry->length);
            }
            catch (const std::exception& ex) {
                Log::Warnf("PMTilesTileDataSource: Failed to load leaf directory: %s", ex.what());
                return false;
            }
            directory = leaf.get();
        }
        return false;
    }

    std::shared_ptr<const PMTilesTileDataSource::Directory> PMTilesTileDataSource::getLeafDirectory(uint64_t offset, uint32_t length) {
        {
            std::lock_guard<std::mutex> lock(_leafMutex);
            auto it = _leafCache.find(offset);
            if (it != _leafCache.end()) {
                _leafOrder.splice(_leafOrder.begin(), _leafOrder, it->second.position);
                return it->second.directory;
            }
        }

        // Decoded outside the lock; two threads missing the same leaf both decode it, the first insert wins
        std::vector<uint8_t> leafData = readRange(_header.leafDirectoriesOffset + offset, length);
        auto directory = std::make_shared<const Directory>(pmtiles::decodeDirectory(pmtiles::decompressData(std::move(leafData), _header.internalCompression)));

        std::lock_guard<std::mutex> lock(_leafMutex);
        auto it = _leafCache.find(offset);
        if (it != _leafCache.end()) {
            return it->second.directory;
        }
        _leafOrder.push_front(offset);
        _leafCache.emplace(offset, CachedLeaf { directory, _leafOrder.begin() });
        _leafCacheEntries += directory->size();
        while (_leafCacheEntries > MAX_CACHED_LEAF_ENTRIES && _leafOrder.size() > 1) {
            auto evicted = _leafCache.find(_leafOrder.back());
            _leafCacheEntries -= evicted->second.directory->size();
            _leafCache.erase(evicted);
            _leafOrder.pop_back();
        }
        return directory;
    }

}

#endif
