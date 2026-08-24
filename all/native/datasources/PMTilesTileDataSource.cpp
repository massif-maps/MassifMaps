#ifdef _MASSIF_OFFLINE_SUPPORT

#include "PMTilesTileDataSource.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "components/Exceptions.h"
#include "datasources/components/PMTilesUtils.h"
#include "projections/Projection.h"
#include "utils/Log.h"

#include <cstring>
#include <algorithm>
#include <unordered_map>
// For JSON parsing (simple extraction)
#include <boost/algorithm/string.hpp>

namespace massif {

    PMTilesTileDataSource::PMTilesTileDataSource(const std::string& path) :
        TileDataSource(),
        _path(path),
        _file(),
        _header(),
        _rootDirectory(),
        _cachedMetadata(),
        _cachedDataExtent(),
        _leafDirectoryCache(),
        _mutex()
    {
        _file = std::make_unique<std::ifstream>(path, std::ios::binary);
        if (!_file->is_open()) {
            throw FileException("Failed to open PMTiles file", path);
        }

        _header = ReadHeader(*_file);
        
        // Read and decode root directory
        std::vector<uint8_t> rootDirData(_header.rootDirectoryLength);
        _file->seekg(_header.rootDirectoryOffset);
        _file->read(reinterpret_cast<char*>(rootDirData.data()), _header.rootDirectoryLength);
        
        std::vector<uint8_t> decompressed = pmtiles::decompressData(rootDirData, _header.internalCompression);
        _rootDirectory = pmtiles::decodeDirectory(decompressed);
        
        Log::Infof("PMTilesTileDataSource: Opened %s with %llu tiles, zoom %d-%d", 
                   path.c_str(), _header.numTileEntries, _header.minZoom, _header.maxZoom);
    }

    PMTilesTileDataSource::PMTilesTileDataSource(int minZoom, int maxZoom, const std::string& path) :
        TileDataSource(minZoom, maxZoom),
        _path(path),
        _file(),
        _header(),
        _rootDirectory(),
        _cachedMetadata(),
        _cachedDataExtent(),
        _leafDirectoryCache(),
        _mutex()
    {
        _file = std::make_unique<std::ifstream>(path, std::ios::binary);
        if (!_file->is_open()) {
            throw FileException("Failed to open PMTiles file", path);
        }

        _header = ReadHeader(*_file);
        
        // Read and decode root directory
        std::vector<uint8_t> rootDirData(_header.rootDirectoryLength);
        _file->seekg(_header.rootDirectoryOffset);
        _file->read(reinterpret_cast<char*>(rootDirData.data()), _header.rootDirectoryLength);
        
        std::vector<uint8_t> decompressed = pmtiles::decompressData(rootDirData, _header.internalCompression);
        _rootDirectory = pmtiles::decodeDirectory(decompressed);
        
        Log::Infof("PMTilesTileDataSource: Opened %s with %llu tiles, zoom %d-%d", 
                   path.c_str(), _header.numTileEntries, _header.minZoom, _header.maxZoom);
    }
        
    PMTilesTileDataSource::~PMTilesTileDataSource() {
    }
    
    std::string PMTilesTileDataSource::getContainerMetaData() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        
        if (!_cachedMetadata) {
            if (_header.metadataLength == 0) {
                _cachedMetadata = "";
                return *_cachedMetadata;
            }
            
            try {
                // Read metadata
                std::vector<uint8_t> metadataData(_header.metadataLength);
                _file->seekg(_header.metadataOffset);
                _file->read(reinterpret_cast<char*>(metadataData.data()), _header.metadataLength);
                
                // Decompress if needed
                std::vector<uint8_t> decompressed = pmtiles::decompressData(metadataData, _header.internalCompression);
                
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
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        
        if (!_cachedDataExtent) {
            // Convert WGS84 bounds to projection bounds
            MapPos minPos = _projection->fromWgs84(MapPos(_header.minLon, _header.minLat));
            MapPos maxPos = _projection->fromWgs84(MapPos(_header.maxLon, _header.maxLat));
            _cachedDataExtent = MapBounds(minPos, maxPos);
        }
        
        return *_cachedDataExtent;
    }

    std::shared_ptr<TileData> PMTilesTileDataSource::loadTile(const MapTile& mapTile) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        
        Log::Infof("PMTilesTileDataSource::loadTile: Loading %s", mapTile.toString().c_str());
        
        if (!_file || !_file->is_open()) {
            Log::Errorf("PMTilesTileDataSource::loadTile: File not open");
            return std::shared_ptr<TileData>();
        }

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
            if (!FindTileEntry(tileId, entry)) {
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
            std::vector<uint8_t> compressedData = ReadTileData(entry.offset, entry.length);
            
            // Decompress if needed
            std::vector<uint8_t> tileBytes = pmtiles::decompressData(compressedData, _header.tileCompression);
            
            auto data = std::make_shared<BinaryData>(tileBytes.data(), tileBytes.size());
            auto tileData = std::make_shared<TileData>(data);
            applyTileMetaData(tileData);
            return tileData;
        }
        catch (const std::exception& ex) {
            Log::Errorf("PMTilesTileDataSource::loadTile: Failed to load tile: %s", ex.what());
            return std::shared_ptr<TileData>();
        }
    }

    pmtiles::Header PMTilesTileDataSource::ReadHeader(std::ifstream& file) {
        // Read 127-byte header
        uint8_t headerBytes[127];
        file.seekg(0);
        file.read(reinterpret_cast<char*>(headerBytes), 127);
        
        if (!file) {
            throw GenericException("Failed to read PMTiles header");
        }
        
        return pmtiles::readHeader(headerBytes);
    }

    std::vector<uint8_t> PMTilesTileDataSource::ReadTileData(uint64_t offset, uint32_t length) {
        std::vector<uint8_t> data(length);
        _file->seekg(_header.tileDataOffset + offset);
        _file->read(reinterpret_cast<char*>(data.data()), length);
        
        if (!_file) {
            throw GenericException("Failed to read tile data");
        }
        
        return data;
    }

    bool PMTilesTileDataSource::FindTileEntry(uint64_t tileId, pmtiles::DirectoryEntry& outEntry) {
        // Search in root directory first
        for (const auto& entry : _rootDirectory) {
            if (entry.runLength == 0) {
                // This is a leaf directory entry
                if (entry.tileId <= tileId) {
                    // Check if this leaf directory might contain our tile
                    try {
                        // Check cache first
                        auto cacheIt = _leafDirectoryCache.find(entry.offset);
                        std::vector<pmtiles::DirectoryEntry>* leafDir;
                        
                        if (cacheIt != _leafDirectoryCache.end()) {
                            leafDir = &cacheIt->second;
                        } else {
                            // Load and cache the leaf directory
                            std::vector<pmtiles::DirectoryEntry> loadedLeafDir = LoadLeafDirectory(entry.offset, entry.length);
                            auto insertResult = _leafDirectoryCache.emplace(entry.offset, std::move(loadedLeafDir));
                            leafDir = &insertResult.first->second;
                        }
                        
                        // Search in the leaf directory using shared utility
                        if (pmtiles::findTileEntry(*leafDir, tileId, outEntry)) {
                            return true;
                        }
                    }
                    catch (const std::exception& ex) {
                        Log::Warnf("PMTilesTileDataSource: Failed to load leaf directory: %s", ex.what());
                    }
                }
            } else {
                // This is a tile entry
                if (tileId >= entry.tileId && tileId < entry.tileId + entry.runLength) {
                    outEntry = entry; // Copy the entry
                    return true;
                }
            }
        }
        
        return false;
    }

    std::vector<pmtiles::DirectoryEntry> PMTilesTileDataSource::LoadLeafDirectory(uint64_t offset, uint32_t length) {
        // Read leaf directory data
        std::vector<uint8_t> leafData(length);
        _file->seekg(_header.leafDirectoriesOffset + offset);
        _file->read(reinterpret_cast<char*>(leafData.data()), length);
        
        if (!_file) {
            throw GenericException("Failed to read leaf directory");
        }
        
        // Decompress and decode using shared utilities
        std::vector<uint8_t> decompressed = pmtiles::decompressData(leafData, _header.internalCompression);
        return pmtiles::decodeDirectory(decompressed);
    }

}

#endif
