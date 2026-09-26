/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_HTTPTILEDATASOURCE_H_
#define _MASSIF_HTTPTILEDATASOURCE_H_

#include "datasources/TileDataSource.h"
#include "datasources/components/PMTilesUtils.h"
#include "network/HTTPClient.h"

#include <random>
#include <string>
#include <map>
#include <vector>
#include <mutex>

namespace massif {

    /**
     * A tile data source that loads tiles using a HTTP connection, replacing the tags in the baseURL template with actual values.
     * Supported tags: s, z, zoom, x, y, xflipped, yflipped, quadkey, frame (e.g. "https://tile.openstreetmap.org/{zoom}/{x}/{y}.png").
     * A URL ending with .pmtiles or starting with pmtiles:// is read from the PMTiles archive with HTTP range requests.
     */
    class HTTPTileDataSource : public TileDataSource {
    public:
        /**
         * Constructs a HTTPTileDataSource object.
         * @param minZoom The minimum zoom level supported by this data source.
         * @param maxZoom The maximum zoom level supported by this data source.
         * @param baseURL The base URL containing tags (for example, "https://tile.openstreetmap.org/{zoom}/{x}/{y}.png").
         */
        HTTPTileDataSource(int minZoom, int maxZoom, const std::string& baseURL);
        virtual ~HTTPTileDataSource();
        
        /**
         * Returns the base URL template containing tags.
         * @return The base URL template.
         */
        std::string getBaseURL() const;
        /**
         * Sets the base URL for the data source.
         * @param baseURL The base URL containing tags (for example, "https://tile.openstreetmap.org/{zoom}/{x}/{y}.png").
         */
        void setBaseURL(const std::string& baseURL);

        /**
         * Returns the subdomains for {s} tag. The default is ["a", "b", "c", "d"].
         * @return The list of subdomains.
         */
        std::vector<std::string> getSubdomains() const;
        /**
         * Sets the subdomains for {s} tag.
         * @param subdomains The list of subdomains to use.
         */
        void setSubdomains(const std::vector<std::string>& subdomains);

        /**
         * Returns true/false based whether the TMS tiling scheme is used.
         * @return True if TMS tiling scheme is used. False if XYZ scheme is used.
         */
        bool isTMSScheme() const;
        /**
         * Enables/disables the TMS tiling scheme. In TMS scheme y coordinate of the tile is flipped. The default is disabled.
         * @param tmsScheme True is TMS tiling scheme should be used. False is XYZ should be used.
         */
        void setTMSScheme(bool tmsScheme);

        /**
         * Returns true/false based on whether the max-age header check is used.
         * If this is enabled, SDK will automatically refresh the tiles when tiles have expired.
         * @return True if max-age header check is used. False otherwise.
         */
        bool isMaxAgeHeaderCheck() const;
        /**
         * Enables/disables the max-age header check.
         * If this is enabled, SDK will automatically refresh the tiles when tiles have expired. The default is disabled.
         * @param maxAgeCheck True if the check should be enabled, false otherwise.
         */
        void setMaxAgeHeaderCheck(bool maxAgeCheck);
        
        /**
         * Returns the current timeout value.
         * @return The current timeout value in seconds. If negative, then default platform-specific timeout is used.
         */
        int getTimeout() const;
        /**
         * Sets the current timeout value.
         * @param timeout The new timeout value in seconds. If negative, then default platform-specific timeout is used.
         */
        void setTimeout(int timeout);

        /**
         * Returns the current set of HTTP headers used. Initially this set is empty and can be changed with setHTTPHeaders.
         * @returns The current set of custom HTTP headers.
         */
        std::map<std::string, std::string> getHTTPHeaders() const;
        /**
         * Sets HTTP headers for all requests. Calling this method will invalidate the datasource and
         * all layers using this data source will be refreshed.
         * @param headers A map of HTTP headers that will be used in subsequent requests.
         */
        void setHTTPHeaders(const std::map<std::string, std::string>& headers);
    
        virtual std::shared_ptr<TileData> loadTile(const MapTile& mapTile);
    
    protected:
        virtual std::string buildTileURL(const std::string& baseURL, const MapTile& tile) const;
        
        bool isPMTilesURL(const std::string& url) const;
        std::string normalizePMTilesURL(const std::string& url) const;
        std::shared_ptr<TileData> loadPMTile(const std::string& baseURL, const MapTile& mapTile);
        pmtiles::Header readPMTilesHeader(const std::string& url);
        std::vector<uint8_t> httpRangeRequest(const std::string& url, uint64_t offset, uint64_t length);
        std::vector<pmtiles::DirectoryEntry> loadPMTilesLeafDirectory(const std::string& url, const pmtiles::Header& header, uint64_t offset, uint32_t length);

        std::string _baseURL;
        std::vector<std::string> _subdomains;
        bool _tmsScheme;
        bool _maxAgeHeaderCheck;
        int _timeout;
        std::map<std::string, std::string> _headers;
        HTTPClient _httpClient;
        mutable std::default_random_engine _randomGenerator;
        mutable std::mutex _mutex;
        
        struct PMTilesCache {
            std::string url;
            pmtiles::Header header;
            std::vector<pmtiles::DirectoryEntry> rootDirectory;
            std::map<uint64_t, std::vector<pmtiles::DirectoryEntry>> leafDirectoryCache;
        };
        mutable std::unique_ptr<PMTilesCache> _pmtilesCache;
    };
    
}

#endif
