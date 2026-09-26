/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ASSETTILEDATASOURCE_H_
#define _MASSIF_ASSETTILEDATASOURCE_H_

#include "datasources/TileDataSource.h"

#include <string>

namespace massif {
    
    /**
     * A tile data source where each map tile is a separate image file bundled with the application.
     * Tags in basePath are replaced with actual values: zoom, x, y, xflipped, yflipped, quadkey.
     * For example, "t{zoom}_{x}_{y}.png" loads tile zoom 2, x 1, y 3 from "t2_1_3.png".
     */
    class AssetTileDataSource : public TileDataSource {
    public:
        /**
         * Constructs an AssetTileDataSource object.
         * @param minZoom The minimum zoom level supported by this data source.
         * @param maxZoom The maximum zoom level supported by this data source.
         * @param basePath The base path containing tags (for example, "t{zoom}_{x}_{y}.png").
         */
        AssetTileDataSource(int minZoom, int maxZoom, const std::string& basePath);
        virtual ~AssetTileDataSource();
    
        virtual std::shared_ptr<TileData> loadTile(const MapTile& tile);
    
    protected:
        virtual std::string buildAssetPath(const std::string& basePath, const MapTile& tile) const;
    
        std::string _basePath;
    };
    
}

#endif
