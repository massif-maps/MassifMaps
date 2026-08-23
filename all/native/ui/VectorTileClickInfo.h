/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VECTORTILECLICKINFO_H_
#define _MASSIF_VECTORTILECLICKINFO_H_

#include "core/MapPos.h"
#include "core/MapTile.h"
#include "ui/ClickInfo.h"
#include "geometry/VectorTileFeature.h"

#include <memory>
#include <string>

namespace massif {
    class Layer;
    
    /**
     * A container class that provides information about a click performed on vector tile feature.
     */
    class VectorTileClickInfo {
    public:
        /**
         * Constructs a VectorTileClickInfo object from a click info, click position, tile information and clicked feature.
         * @param clickInfo The click info.
         * @param clickPos The click position in the coordinate system of the data source.
         * @param featureClickPos The click position in the coordinate system of the data source that corresponds to feature point.
         * @param feature The clicked vector tile feature.
         * @param layer The layer on which the click was performed.
         */
        VectorTileClickInfo(const ClickInfo& clickInfo, const MapPos& clickPos, const MapPos& featureClickPos, const std::shared_ptr<VectorTileFeature>& feature, const std::shared_ptr<Layer>& layer, int featurePosIndex);
        virtual ~VectorTileClickInfo();
    
        /**
         * Returns the click type.
         * @return The type of the click performed.
         */
        ClickType::ClickType getClickType() const;

        /**
         * Returns the click info.
         * @return The attributes of the click.
         */
        const ClickInfo& getClickInfo() const;

        /**
         * Returns the click position.
         * @return The click position in the coordinate system of the data source.
         */
        const MapPos& getClickPos() const;
        
        /**
         * Returns the position on the clicked feature, that is close to the click position.
         * For points it will always be the center position, for lines it will be the closest point
         * on the line, for billboards it will be the anchor point and for polygons it's equal to
         * getClickPos().
         * @return The feature click position in the coordinate system of the data source.
         */
        const MapPos& getFeatureClickPos() const;

        /**
         * Returns the position of the clicked feature.
         *
         * For a MultiPoint this is the point that was clicked, not the centre of the whole set -
         * which getFeatureClickPos deliberately does not cover, and which a caller otherwise has
         * to reconstruct from getFeaturePosIndex plus a downcast.
         * @return The feature position, in the coordinate system of the data source.
         */
        MapPos getFeaturePos() const;

        /**
         * Returns the tile id of the clicked feature.
         * @return The tile id of the clicked feature.
         */
        const MapTile& getMapTile() const;

        /**
         * Returns the id of the clicked feature.
         * @return The id of the clicked feature.
         */
        long long getFeatureId() const;
        /**
         * In case of MultiPoint PointGeometry this will return the index of the clicked position
         * @return The index
         */
        int getFeaturePosIndex() const;
    
        /**
         * Returns the clicked feature.
         * @return The feature on which the click was performed.
         */
        std::shared_ptr<VectorTileFeature> getFeature() const;

        /**
         * Returns the name of the layer of the clicked feature.
         * Note that this is the layer name in the tile, not the name of style layer.
         * @return The name of the layer of the clicked feature.
         */
        const std::string& getFeatureLayerName() const;

        /**
         * Returns the layer of the vector tile.
         * @return The layer of the vector tile.
         */
        std::shared_ptr<Layer> getLayer() const;
    
    private:
        ClickInfo _clickInfo;
        MapPos _clickPos;
        MapPos _featureClickPos;
        int _featurePosIndex;
        std::shared_ptr<VectorTileFeature> _feature;
        std::shared_ptr<Layer> _layer;
    };
    
}

#endif
