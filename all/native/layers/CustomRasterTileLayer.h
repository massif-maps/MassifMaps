/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CUSTOMRASTERTILELAYER_H_
#define _MASSIF_CUSTOMRASTERTILELAYER_H_

#include "core/MapTile.h"
#include "core/Variant.h"
#include "layers/RasterTileLayer.h"
#include "layers/WeakSharedMap.h"

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <vt/TileBitmap.h>

namespace massif {

    /**
     * A raster tile layer (the general form of HillshadeRasterTileLayer) rendering each tile through a GLSL shader defining
     * `vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity)`; it may use
     * getRawColor(), getMapZoom(), vUV, uUVScale and uBitmap, and returns a premultiplied color. Note: experimental, may change.
     *
     * Up to 4 sources are drawn in one pass (see addDataSource): sampleRaster(i) reads source i, i being a literal, and
     * sampleRaster(0) is getRawColor(). A source whose tile is not loaded yet reads as source 0. Every parameter set with
     * setShaderParameter() is a `uniform float` of the same name. Sources must share a tile size and zoom range.
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

        /**
         * Adds a source drawn together with the layer's own, read in the shader as sampleRaster(getDataSourceCount() - 1).
         * Not used by HillshadeRasterTileLayer.
         * @param dataSource The raster data source; at most 4 sources in total.
         */
        void addDataSource(const std::shared_ptr<TileDataSource>& dataSource);
        /**
         * Returns the number of sources, the layer's own included.
         * @return The number of sources.
         */
        int getDataSourceCount() const;

        /**
         * Sets a `uniform float` of the shader, e.g. a blend factor driven by a clock.
         * @param name The uniform name.
         * @param value The value.
         */
        void setShaderParameter(const std::string& name, float value);
        /**
         * Returns a shader parameter.
         * @param name The uniform name.
         * @return The value, or 0 when it was never set.
         */
        float getShaderParameter(const std::string& name) const;
        /**
         * Returns all shader parameters, as numbers.
         * @return The shader parameters by name.
         */
        std::map<std::string, Variant> getShaderParameters() const;
        /**
         * Sets shader parameters; the ones not named keep their value.
         * @param parameters The shader parameters by name; values that are not numbers read as 0.
         */
        void setShaderParameters(const std::map<std::string, Variant>& parameters);

    protected:
        virtual bool onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState);

        virtual std::shared_ptr<vt::Tile> createVectorTile(const MapTile& subTile, const MapTile& tile, const std::shared_ptr<TileData>& tileData, const std::shared_ptr<Bitmap>& bitmap, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const;
        virtual std::shared_ptr<vt::Tile> createVectorTile(const MapTile& subTile, const MapTile& tile, const std::shared_ptr<TileData>& tileData, const std::shared_ptr<Bitmap>& bitmap, const std::vector<std::shared_ptr<vt::TileBitmap> >& extraBitmaps, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const;
        virtual bool loadExtraBitmaps(const MapTile& dataSourceTile, const CancelableTask& task, std::vector<std::shared_ptr<vt::TileBitmap> >& extraBitmaps) const;
        // Whether a tile with these bytes is already a live bitmap, so decoding it again would only be thrown away.
        bool isSharedBitmap(const std::shared_ptr<TileData>& tileData) const;

        virtual void registerDataSourceListener();
        virtual void unregisterDataSourceListener();
        virtual std::size_t drapeStackSignature() const;

        struct Input {
            std::shared_ptr<TileDataSource> dataSource;
            MapTile tile;
        };
        // What is drawn with the tile of the layer's own source; AnimatedRasterTileLayer points it at the next frame.
        virtual std::vector<Input> getExtraInputs(const MapTile& dataSourceTile) const;

        // Falls back to PASSTHROUGH_SHADER.
        virtual std::string getEffectiveShaderSource() const;

        static const std::string PASSTHROUGH_SHADER;
        static const int DRAPE_REBAKE_MIN_MS = 100;

        std::string _shaderSource;
        std::vector<std::shared_ptr<TileDataSource> > _extraDataSources;
        std::map<std::string, float> _shaderParameters;
        // What the drape stack was last told about the shader and parameters: a bake is a whole tile, so it follows at a bounded rate.
        mutable std::size_t _publishedDrapeHash;
        mutable std::chrono::steady_clock::time_point _publishedDrapeAt;
        mutable std::recursive_mutex _customShaderMutex;

    private:
        static std::shared_ptr<vt::TileBitmap> MakeTileBitmap(const std::shared_ptr<Bitmap>& bitmap);
        std::shared_ptr<vt::Tile> buildTile(const MapTile& tile, const std::vector<std::shared_ptr<vt::TileBitmap> >& bitmaps, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const;
        // Keyed by the tile's bytes: a frame is the second raster of one tile and the first of the next, and both must upload once.
        typedef std::pair<std::size_t, std::size_t> ContentKey;
        static ContentKey MakeContentKey(const std::shared_ptr<TileData>& tileData);
        mutable WeakSharedMap<ContentKey, vt::TileBitmap> _sharedBitmaps;
    };

}

#endif
