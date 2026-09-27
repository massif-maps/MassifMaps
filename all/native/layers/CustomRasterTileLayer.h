/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CUSTOMRASTERTILELAYER_H_
#define _MASSIF_CUSTOMRASTERTILELAYER_H_

#include "layers/RasterTileLayer.h"

#include <mutex>
#include <string>

namespace massif {

    /**
     * A raster tile layer (the general form of HillshadeRasterTileLayer) rendering each tile through a GLSL shader defining
     * `vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity)`; it may use
     * getRawColor(), getMapZoom(), vUV, uUVScale and uBitmap, and returns a premultiplied color. Note: experimental, may change.
     */
    class CustomRasterTileLayer : public RasterTileLayer {
    public:
        /**
         * Constructs a CustomRasterTileLayer object from a raster data source.
         * @param dataSource The raster data source from which this layer loads data.
         */
        explicit CustomRasterTileLayer(const std::shared_ptr<TileDataSource>& dataSource);
        virtual ~CustomRasterTileLayer();

        /**
         * Returns the custom fragment shader source.
         * @return The shader source. Empty means the default passthrough (outputs the raw tile).
         */
        std::string getShaderSource() const;
        /**
         * Sets the custom fragment shader source (the "filter"). See the class description for the
         * required entry point and available helpers. Empty resets to the passthrough shader.
         * @param shaderSource The GLSL shader source.
         */
        void setShaderSource(const std::string& shaderSource);

    protected:
        virtual bool onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState);

        virtual std::shared_ptr<vt::Tile> createVectorTile(const MapTile& subTile, const MapTile& tile, const std::shared_ptr<TileData>& tileData, const std::shared_ptr<Bitmap>& bitmap, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const;

        // Falls back to PASSTHROUGH_SHADER.
        virtual std::string getEffectiveShaderSource() const;

        static const std::string PASSTHROUGH_SHADER;

        std::string _shaderSource;
        mutable std::recursive_mutex _customShaderMutex;
    };

}

#endif
