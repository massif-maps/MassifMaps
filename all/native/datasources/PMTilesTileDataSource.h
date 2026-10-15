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

#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

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
        using Directory = std::vector<pmtiles::DirectoryEntry>;

        struct CachedLeaf {
            std::shared_ptr<const Directory> directory;
            std::list<uint64_t>::iterator position;
        };

        void openArchive();
        std::vector<uint8_t> readRange(uint64_t offset, uint64_t length) const;
        bool findTileEntry(uint64_t tileId, pmtiles::DirectoryEntry& outEntry);
        std::shared_ptr<const Directory> getLeafDirectory(uint64_t offset, uint32_t length);

        static constexpr std::size_t MAX_CACHED_LEAF_ENTRIES = 1 << 20;

        std::string _path;
#ifdef _WIN32
        mutable std::ifstream _file;
        mutable std::mutex _fileMutex;
#else
        int _fd = -1;
#endif
        pmtiles::Header _header;
        Directory _rootDirectory;
        mutable std::optional<std::string> _cachedMetadata;
        mutable std::optional<MapBounds> _cachedDataExtent;
        mutable std::mutex _cacheMutex;

        std::unordered_map<uint64_t, CachedLeaf> _leafCache;
        std::list<uint64_t> _leafOrder;
        std::size_t _leafCacheEntries = 0;
        std::mutex _leafMutex;
    };
    
}

#endif

#endif
