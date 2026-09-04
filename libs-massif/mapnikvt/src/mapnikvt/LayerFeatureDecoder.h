/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_LAYERFEATUREDECODER_H_
#define _MASSIF_MAPNIKVT_LAYERFEATUREDECODER_H_

#include "FeatureDecoder.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <cglib/bbox.h>
#include <cglib/mat.h>

namespace massif::mvt {
    /**
     * A feature decoder for tile formats whose features are addressed by layer name - MVT and MLT.
     * Holds the tile-to-target transform and the clip box both of them need, so LayerTileReader
     * can read either through this one interface.
     */
    class LayerFeatureDecoder : public FeatureDecoder {
    public:
        void setTransform(const cglib::mat3x3<float>& transform) {
            if (transform != _transform) {
                _transform = transform;
                invalidateGeometryCache();
            }
        }

        void setClipBox(const cglib::bbox2<float>& clipBox) {
            if (clipBox != _clipBox) {
                _clipBox = clipBox;
                invalidateGeometryCache();
            }
        }

        void setFeatureIdOverride(bool featureIdOverride, long long tileIdOffset = 0) {
            _featureIdOverride = featureIdOverride;
            _tileIdOffset = tileIdOffset;
        }

        /**
         * The box the tile's own data covers, in the coordinates the features come out in. The unit
         * square under the identity transform, an ancestor's box under overzoom - the edges the
         * server cut the geometry at, which is what an anchor shared across a cut has to agree on.
         */
        cglib::bbox2<float> getSourceBox() const {
            cglib::bbox2<float> box = cglib::bbox2<float>::smallest();
            for (int i = 0; i < 4; i++) {
                box.add(cglib::transform_point(cglib::vec2<float>(i & 1 ? 1.0f : 0.0f, i & 2 ? 1.0f : 0.0f), _transform));
            }
            return box;
        }

        virtual std::vector<std::string> getLayerNames() const = 0;

        virtual bool hasLayer(const std::string& name) const = 0;

        // `clip` off yields every feature of the layer, whatever the clip box - the anchor pass
        // needs the parts of a building this tile does not draw. It runs on its own caches, so it
        // cannot leak an out-of-box geometry into the drawing pass.
        virtual std::shared_ptr<FeatureIterator> createLayerFeatureIterator(const std::string& name, const std::set<std::string>* fields, bool clip = true) const = 0;

        virtual bool findFeature(long long localId, std::string& layerName, Feature& feature) const = 0;

    protected:
        virtual void invalidateGeometryCache() = 0;

        cglib::mat3x3<float> _transform = cglib::mat3x3<float>::identity();
        cglib::bbox2<float> _clipBox = cglib::bbox2<float>(cglib::vec2<float>(-0.125f, -0.125f), cglib::vec2<float>(1.125f, 1.125f));
        bool _featureIdOverride = false;
        long long _tileIdOffset = 0;
    };
}

#endif
