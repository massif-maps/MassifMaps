/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_PMTILESTILEDATASOURCE_H_
#define _MASSIF_PMTILESTILEDATASOURCE_H_

#ifdef _MASSIF_OFFLINE_SUPPORT

#include "datasources/TileDataSource.h"
#include "datasources/components/PMTilesUtils.h"

#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <fstream>
#include <unordered_map>

namespace massif {
    
    /**
     * A tile data source that loads tiles from a PMTiles v3 archive file.
     * PMTiles is a single-file archive format for pyramids of tiled data.
     * The archive format supports efficient random access and metadata storage.
     */
    class PMTilesTileDataSource : public TileDataSource {
    public:
        /**
         * Constructs a PMTilesTileDataSource object.
         * Min and max zoom levels are automatically detected from the PMTiles header.
         * @param path The path to the PMTiles archive file.
         * @throws std::exception If the file could not be opened or is not a valid PMTiles archive.
         */
        explicit PMTilesTileDataSource(const std::string& path);
        
        /**
         * Constructs a PMTilesTileDataSource object with specified zoom levels.
         * @param minZoom The minimum zoom level supported by this data source.
         * @param maxZoom The maximum zoom level supported by this data source.
         * @param path The path to the PMTiles archive file.
         * @throws std::exception If the file could not be opened or is not a valid PMTiles archive.
         */
        PMTilesTileDataSource(int minZoom, int maxZoom, const std::string& path);
    
        virtual ~PMTilesTileDataSource();
        
        /**
         * Get PMTiles archive metadata information as a JSON string.
         * The metadata is stored in the PMTiles archive and may contain
         * information such as name, description, attribution, vector layers, etc.
         * @return JSON string containing metadata, or empty string if no metadata exists.
         */
        std::string getContainerMetaData() const;
        
        /**
         * Get a specific metadata value by key from the parsed JSON metadata.
         * @param key The metadata key to retrieve (e.g., "name", "description", "attribution").
         * @return The metadata value as a string, or empty string if not found.
         */
        virtual std::string getContainerMetaData(const std::string& key) const;

        virtual int getMinZoom() const;

        virtual int getMaxZoom() const;

        virtual MapBounds getDataExtent() const;

        virtual std::shared_ptr<TileData> loadTile(const MapTile& mapTile);

    private:
        static pmtiles::Header ReadHeader(std::ifstream& file);
        
        std::vector<uint8_t> ReadTileData(uint64_t offset, uint32_t length);
        bool FindTileEntry(uint64_t tileId, pmtiles::DirectoryEntry& outEntry);
        std::vector<pmtiles::DirectoryEntry> LoadLeafDirectory(uint64_t offset, uint32_t length);

        std::string _path;
        std::unique_ptr<std::ifstream> _file;
        pmtiles::Header _header;
        std::vector<pmtiles::DirectoryEntry> _rootDirectory;
        mutable std::optional<std::string> _cachedMetadata;
        mutable std::optional<MapBounds> _cachedDataExtent;
        mutable std::unordered_map<uint64_t, std::vector<pmtiles::DirectoryEntry> > _leafDirectoryCache;
        mutable std::recursive_mutex _mutex;
    };
    
}

#endif

#endif
