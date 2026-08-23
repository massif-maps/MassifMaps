/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_HILLSHADERASTERTILELAYER_H_
#define _MASSIF_HILLSHADERASTERTILELAYER_H_

#include "graphics/Color.h"
#include "components/DirectorPtr.h"
#include "layers/CustomRasterTileLayer.h"
#include "rastertiles/ElevationDecoder.h"

#include <atomic>

namespace massif {
    class ElevationManager;

    namespace HillshadeMethod {
        /**
        * Hillshade rendering method.
        */
        enum HillshadeMethod {
            /**
            * MapLibre's legacy hillshade algorithm.
            */
            STANDARD,
            /**
            * Combined hillshade algorithm based on GDAL.
            */
            COMBINED,
            /**
            * Igor hillshade algorithm based on GDAL.
            */
            IGOR,
            /**
            * Multi-directional hillshade based on GDAL: four light sources at 225, 270, 315 and 360
            * degrees weighted by the aspect. Ignores the illumination azimuth, uses only its altitude.
            */
            MULTIDIRECTIONAL,
            /**
            * Basic hillshade algorithm based on GDAL.
            */
            BASIC
        };
    }
    
    /**
     * A tile layer that displays an overlay hillshading. Should be used together with corresponding data source that encodes height in RGBA image.
     * The shading is based on the direction of the main light source, which can be configured using Options class.
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class HillshadeRasterTileLayer : public CustomRasterTileLayer {
    public:
        /**
         * Constructs a HillshadeRasterTileLayer object from a data source.
         * @param dataSource The data source from which this layer loads data.
         */
        explicit HillshadeRasterTileLayer(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<ElevationDecoder>& elevationDecoder);
        explicit HillshadeRasterTileLayer(const std::shared_ptr<TileDataSource>& dataSource);
        virtual ~HillshadeRasterTileLayer();

        /**
         * Returns the contrast of the hillshade overlay. This is the equivalent of MapLibre's
         * 'hillshade-exaggeration' paint property: it controls the slope response curve and the
         * overall strength of the shading, not the relief itself.
         * @return The contrast value (between 0..1). Default is 0.5.
         */
        float getContrast() const;
        /**
         * Sets the contrast of the hillshade overlay. Equivalent to MapLibre's
         * 'hillshade-exaggeration'; 0.5 is the neutral value.
         * @param contrast The contrast value (between 0..1).
         */
        void setContrast(float contrast);

        /**
         * Returns the height scale of the hillshade overlay.
         * @return The relative height scale. Default is 0.05.
         */
        float getHeightScale() const;
        /**
         * Sets the height scale of the hillshade overlay. Baked into the normal map at decode time,
         * so changing it reloads the tiles. See setLegacyHeightScaleEnabled for the old default.
         * @param heightScale The relative height scale. Actual height is multiplied by this values.
         */
        void setHeightScale(float heightScale);

        /**
         * Returns the per-frame relief exaggeration factor, i.e. the vertical exaggeration of the
         * slope. Unlike height scale this is a shader uniform applied at render time (no tile
         * re-decode), so it can be animated smoothly.
         * @return The exaggeration factor. Default is 1.0.
         */
        float getExaggeration() const;
        /**
         * Sets the per-frame relief exaggeration factor. Multiplies the hillshade slope in the
         * shader without re-decoding tiles, so it can change smoothly (e.g. with zoom). 1.0 leaves the
         * appearance unchanged.
         * @param exaggeration The exaggeration factor.
         */
        void setExaggeration(float exaggeration);

        /**
         * Returns the shading color of areas that face away from the light source.
         * @return The shadow color of the layer.
         */
        Color getShadowColor() const;
        /**
         * Sets the shading color of areas that face away from the light source.
         * @param color The new shadow color of the layer.
         */
        void setShadowColor(const Color& color);
        /**
         * Returns the shading color used to accentuate rugged terrain like sharp cliffs and gorges.
         * @return The accent color of the layer.
         */
        Color getAccentColor() const;
        /**
         * Sets the shading color used to accentuate rugged terrain like sharp cliffs and gorges.
         * @param color The new accent color of the layer.
         */
        void setAccentColor(const Color& color);

        /**
         * Returns the shading color of areas that faces towards the light source.
         * @return The highlight color of the layer.
         */
        Color getHighlightColor() const;

        /**
         * Sets the shading color of areas that faces towards the light source.
         * @param color The new highlight color of the layer.
         */
        void setHighlightColor(const Color& color);

        std::string getNormalMapLightingShader() const;
        /**
         * Sets a custom normalmap lighting shader.
         * @param shader The custom shader.
         */
        void setNormalMapLightingShader(const std::string& shader);

        /**
         * Returns the illumination direction of the layer.
         * @return The direction vector for the hillshade illumination
         */
        MapVec getIlluminationDirection() const;
        /**
         * Sets the illumination direction.
         * The horizontal part is read as a compass bearing (x = sin(azimuth), y = cos(azimuth), with
         * azimuth 0 = north, increasing clockwise) pointing towards the light, and z points down
         * towards the ground: -sin(altitude). MapLibre's default 'hillshade-illumination-direction'
         * of 335 degrees at a 45 degree altitude is therefore (-0.4226, 0.9063, -0.7071), which is
         * the default here too.
         * @param The new direction vector for the illumination light. (0,0,-1) means straight down, (-0.707,0,-0.707) means
         *        from east with a 45 degree angle. The direction vector will be normalized.
         *        Note that the MULTIDIRECTIONAL method ignores the azimuth and uses only the altitude.
         */
        void setIlluminationDirection(MapVec direction);
        /**
         * Returns wheter the illumination direction should change with the map rotation.
         * @return enabled
         */
        bool getIlluminationMapRotationEnabled() const;
        /**
         * Sets wheter the illumination direction should change with the map rotation.
         * @param enabled whether to enable or not.
         */
        void setIlluminationMapRotationEnabled(bool enabled);
        /**
         * Returns the normal vector tile should be exagerated based on the zoom level.
         * @return enabled
         */
        bool getExagerateHeightScaleEnabled() const;

        /**
         * Sets wheter the normal vector tile should be exagerated based on the zoom level.
         * @param enabled whether to enable or not.
         */
        void setExagerateHeightScaleEnabled(bool enabled);

        /**
         * Returns whether the legacy (pre-MapLibre-parity) height scale formula is used.
         * @return True if the legacy formula is used. Default is false.
         */
        bool isLegacyHeightScaleEnabled() const;

        /**
         * Sets whether to use the legacy (pre-MapLibre-parity) height scale formula, in which the
         * relief is damped by the absolute zoom level and therefore flattens as the camera zooms in.
         * The default formula instead follows MapLibre: the true slope from zoom 15 up, boosted
         * below it. Styles tuned against the legacy formula should enable this and also call
         * setHeightScale(0.09f), which was the old default height scale.
         * @param enabled Whether to use the legacy formula.
         */
        void setLegacyHeightScaleEnabled(bool enabled);

        /**
         * Returns the hillshade rendering method.
         * @return The hillshade method. Default is IGOR.
         */
        HillshadeMethod::HillshadeMethod getHillshadeMethod() const;
        /**
         * Sets the hillshade rendering method.
         * @param method The hillshade method to use.
         */
        void setHillshadeMethod(HillshadeMethod::HillshadeMethod method);

        /**
         * Returns whether the normal map encodes absolute elevation (so a custom normal-map lighting
         * shader can call getElevation()).
         * @return True if elevation encoding is enabled. Default is false.
         */
        bool isElevationEncodingEnabled() const;
        /**
         * Sets whether the normal map encodes absolute elevation in addition to the surface normal.
         * Required for a custom normal-map lighting shader that reads getElevation()/getMapZoom() (e.g.
         * to draw its own per-zoom contour lines). Enabling contour lines turns this on implicitly.
         * @param enabled True to encode elevation into the normal map.
         */
        void setElevationEncodingEnabled(bool enabled);

        /**
         * Returns whether GPU contour lines are drawn over the hillshade.
         * @return True if contour lines are enabled. Default is false.
         */
        bool isContourEnabled() const;
        /**
         * Sets whether to draw anti-aliased contour lines over the hillshade, computed in the shader
         * from the elevation data. Enabling this makes the normal map encode absolute elevation (which
         * is also available to a custom normal-map lighting shader). Note: this class is experimental.
         * @param enabled True to draw contour lines.
         */
        void setContourEnabled(bool enabled);
        /**
         * Returns the spacing between contour lines in meters.
         * @return The contour interval in meters. Default is 100.
         */
        float getContourInterval() const;
        /**
         * Sets the spacing between contour lines in meters.
         * @param interval The contour interval in meters.
         */
        void setContourInterval(float interval);
        /**
         * Returns the contour line color.
         * @return The contour line color.
         */
        Color getContourColor() const;
        /**
         * Sets the contour line color.
         * @param color The contour line color.
         */
        void setContourColor(const Color& color);
        /**
         * Returns the contour line half-width in screen pixels.
         * @return The contour line width. Default is 0.75.
         */
        float getContourWidth() const;
        /**
         * Sets the contour line half-width in screen pixels.
         * @param width The contour line width in pixels.
         */
        void setContourWidth(float width);

        /**
         * Returns whether the layer may shade the 3D terrain's own elevation texture instead of
         * loading a DEM tile set of its own.
         * @return True if terrain paint mode is allowed. Default is true.
         */
        bool isTerrainPaintEnabled() const;
        /**
         * Sets whether the layer may shade the shared 3D terrain elevation texture instead of
         * loading, decoding and uploading a DEM tile set of its own. It applies only when the map
         * renders 3D terrain with draped fills FROM THE SAME data source, and not while the
         * built-in contour lines are enabled: the layer then draws one quad per terrain tile, at
         * its own place in the layer order, and fetches nothing. In any other configuration the
         * layer keeps its normal map tile set. Disable it to compare the two paths.
         * Note that the shading is then computed from the TERRAIN's elevation grid, so it does not
         * follow this layer's own zoom level bias, and it resolves the relief slightly differently
         * from a magnified normal map raster.
         * @param enabled True to allow terrain paint mode.
         */
        void setTerrainPaintEnabled(bool enabled);

        /**
         * Returns whether the terrain paint shades from the elevation source's own maximum zoom.
         * @return True if full DEM detail is used. Default is true.
         */
        bool isTerrainPaintFullDetailEnabled() const;
        /**
         * Sets whether the terrain paint shades from the elevation source's own maximum zoom
         * instead of the coarser level the terrain MESH needs (one texel per half surface cell,
         * which costs two zoom levels of relief - at high zoom, all of it). Shading is per fragment
         * and resolves what the mesh cannot, so this is on by default; turning it off gives the
         * terrain's own elevation textures back and is measurably faster.
         * @param enabled True to shade from the DEM's own maximum zoom.
         */
        void setTerrainPaintFullDetailEnabled(bool enabled);

        double getElevation(const MapPos& pos) const;
        std::vector<double> getElevations(const std::vector<MapPos> poses) const;

    protected:
        virtual bool onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState);
        virtual bool prepareTerrainDrapeFrame(float deltaSeconds, const ViewState& viewState);
        virtual void loadData(const std::shared_ptr<CullState>& cullState);
        virtual std::size_t drapeStackSignature() const;
        virtual bool paintsEveryDrapeTile() const;

        virtual std::shared_ptr<vt::Tile> createVectorTile(const MapTile& subTile, const MapTile& tile, const std::shared_ptr<TileData>& tileData, const std::shared_ptr<Bitmap>& bitmap, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const;

        std::shared_ptr<Bitmap> getTileDataBitmap(std::shared_ptr<TileData> tileData) const;
        std::shared_ptr<ElevationManager> getElevationManager() const;

        const std::shared_ptr<ElevationDecoder> _elevationDecoder;

        mutable std::shared_ptr<ElevationManager> _elevationManager;
   
        std::atomic<float> _contrast;
        std::atomic<bool> _exagerateHeightScaleEnabled;
        std::atomic<bool> _legacyHeightScaleEnabled;
        std::atomic<float> _heightScale;

        std::atomic<float> _exaggeration;
        std::string _normalMapLightingShader;
        std::atomic<Color> _shadowColor;
        std::atomic<Color> _accentColor;
        std::atomic<Color> _highlightColor;
        std::atomic<MapVec> _illuminationDirection;
        std::atomic<bool> _illuminationMapRotationEnabled;
        std::atomic<HillshadeMethod::HillshadeMethod> _hillshadeMethod;
        std::atomic<bool> _contourEnabled;
        std::atomic<bool> _elevationEncodingEnabled;
        std::atomic<float> _contourInterval;
        std::atomic<Color> _contourColor;
        std::atomic<float> _contourWidth;
        std::atomic<bool> _terrainPaintEnabled;
        std::atomic<bool> _terrainPaintFullDetailEnabled;

        // Whether the layer shades the shared terrain elevation texture this frame instead of its
        // own tile set: 3D terrain with draped fills, over the SAME data source (a different DEM
        // would silently be replaced by the terrain's one).
        bool isTerrainPaintActive() const;
        // Pushes every appearance value onto the tile renderer. Called both before the shared
        // drape bake and from the layer's own draw, so the paint and the normal map agree.
        void applyRendererSettings() const;
        // Hash of everything the paint's appearance depends on - including what only the lighting
        // shader sees - so cached drape textures are re-baked when any of it changes.
        std::size_t calculatePaintFingerprint() const;
        // Map rotation at the last prepared frame, quantised. The paint is BAKED, so when the
        // illumination follows the map the bake has to be redone as the map turns; the normal map
        // path only had to change a uniform. Quantised so that a slow rotation does not re-bake
        // every frame for a light direction nobody can tell apart.
        std::atomic<int> _paintRotationStep;

        // Elevation is packed into the normal map when contours are on or when explicitly requested
        // for a custom shader.
        bool isElevationEncoded() const { return _contourEnabled.load() || _elevationEncodingEnabled.load(); }
    };
    
}

#endif
