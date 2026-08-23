/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VECTORTILESEARCHSERVICE_H_
#define _MASSIF_VECTORTILESEARCHSERVICE_H_

#ifdef _MASSIF_SEARCH_SUPPORT

#include "search/SearchRequest.h"

#include <memory>
#include <mutex>
#include <vector>

namespace massif {
    class Projection;
    class TileDataSource;
    class VectorTileDecoder;
    class VectorTileFeatureCollection;

    /**
     * A search service for finding features from the specified vector tile data source.
     * Depending on the datasource, searching may perform network requests and must be executed in non-UI background thread.
     */
    class VectorTileSearchService {
    public:
        /**
         * Constructs a VectorTileSearchService for the given vector data source and vector tile decoder.
         * @param dataSource The vector data source to search from.
         * @param tileDecoder The vector tile decoder to use for decoding the tiles.
         */
        VectorTileSearchService(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<VectorTileDecoder>& tileDecoder);
        virtual ~VectorTileSearchService();

        /**
         * Returns the tile data source of the search service.
         * @return The tile data source of the search service.
         */
        const std::shared_ptr<TileDataSource>& getDataSource() const;

        /**
         * Returns the projection the results are expressed in, which is the data source's.
         * @return The projection of the found features' positions.
         */
        std::shared_ptr<Projection> getProjection() const;

        /**
         * Returns the tile decoder used by the search service.
         * @return The tile decoder used by the search service.
         */
        const std::shared_ptr<VectorTileDecoder>& getTileDecoder() const;

        /**
         * Returns the minimum zoom level of vector tiles used.
         * By default the minimum zoom level is specified by data source and is usually 0.
         * @return The minimum zoom level of vector tiles used.
         */
        int getMinZoom() const;
        /**
         * Sets the minimum zoom level of vector tiles used.
         * @param minZoom The new minimum zoom level.
         */
        void setMinZoom(int minZoom);

        /**
         * Returns the maximum zoom level of vector tiles used.
         * By default the maximum zoom level is specified by data source.
         * @return The maximum zoom level of vector tiles used.
         */
        int getMaxZoom() const;
        /**
         * Sets the maximum zoom level of vector tiles used.
         * @param maxZoom The new maximum zoom level.
         */
        void setMaxZoom(int maxZoom);

        /**
         * Returns the maximum number of results the search service returns.
         * @return The maximum number of results the search service returns.
         */
        int getMaxResults() const;
        /**
         * Sets the maximum number of results the search service returns.
         * The default number of results is 1000.
         * @param maxResults The new maximum number of results the geocoding service returns.
         */
        void setMaxResults(int maxResults);

        /**
         * Returns wether result features are sorted by distance
         * @return wether result features are sorted by distance
         */
        bool getSortByDistance() const;
        /**
         * Sets wether to sort result features by distance
         * @param sortByDistance wether to sort result features by distance
         */
        void setSortByDistance(bool sortByDistance);
        /**
         * Returns wether to prevent duplicate elements
         * @return wether to prevent duplicate elements
         */
        bool getPreventDuplicates() const;
        /**
         * Sets wether to prevent duplicate elements (present in different tiles)
         * @param preventDuplicates wether to prevent duplicate elements
         */
        void setPreventDuplicates(bool preventDuplicates);


        /**
         * Returns the layers to filter while decoding tiles.
         * @return The list of layers.
         */
        std::vector<std::string> getLayers() const;
        /**
         * Sets the layers to filter.
         * @param subdomains The list of layers to filter.
         */
        void setLayers(const std::vector<std::string>& layers);

        /**
         * Searches for the features specified by search request from the vector tiles bound to the service.
         * The zoom level range used for searching is specified using minZoom/maxZoom attributes of the search service.
         * Depending on the data source, this method may perform slow IO operations and may need to be run in background thread.
         * @param request The search request containing search filters.
         * @return The resulting feature collection containing features matching the request.
         */
        virtual std::shared_ptr<VectorTileFeatureCollection> findFeatures(const std::shared_ptr<SearchRequest>& request) const;

    protected:
        const std::shared_ptr<TileDataSource> _dataSource;
        const std::shared_ptr<VectorTileDecoder> _tileDecoder;

        int _minZoom;
        int _maxZoom;
        int _maxResults;
        bool _sortByDistance;
        bool _preventDuplicates;
        std::vector<std::string> _layers;

        mutable std::mutex _mutex;
    };
    
}

#endif

#endif
