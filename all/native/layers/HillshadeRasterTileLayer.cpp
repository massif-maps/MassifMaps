#include "HillshadeRasterTileLayer.h"
#include "components/Options.h"
#include "components/TerrainOptions.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "renderers/drawdatas/TileDrawData.h"
#include "utils/Log.h"
#include "utils/TileUtils.h"
#include "core/BinaryData.h"
#include "core/Variant.h"
#include "datasources/components/TileData.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"
#include "projections/EPSG3857.h"
#include "projections/Projection.h"
#include "terrain/ElevationManager.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <functional>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include "graphics/Bitmap.h"

#include <vt/TileId.h>
#include <vt/Tile.h>
#include <vt/TileTransformer.h>
#include <vt/TileBitmap.h>
#include <vt/TileLayer.h>
#include <vt/TileLayerBuilder.h>
#include <vt/NormalMapBuilder.h>
#include <vt/TileLayerBuilder.h>

namespace massif
{

#ifdef __ANDROID__
    // Interleaved A/B of the paint against the normal-map tile set, without a rebuild and
    // without reaching into a composite layer's internal hillshade child:
    //   adb shell setprop debug.massif.terrainpaint 0
    static bool isTerrainPaintDisabledByProperty() {
        static const bool disabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return __system_property_get("debug.massif.terrainpaint", property) > 0 && property[0] == '0';
        }();
        return disabled;
    }
#else
    static bool isTerrainPaintDisabledByProperty() {
        return false;
    }
#endif

    HillshadeRasterTileLayer::HillshadeRasterTileLayer(const std::shared_ptr<TileDataSource> &dataSource, const std::shared_ptr<ElevationDecoder> &elevationDecoder) : CustomRasterTileLayer(dataSource),
        _elevationDecoder(elevationDecoder),
        _contrast(0.5f),
        // 0.05, not 1.0: at 1.0 real DEM relief saturates to black and white and the shading reads
        // as a stencil. This is the value the demo and every terrain screenshot here use.
        _heightScale(0.05f),
        _exaggeration(1.0f),
        _exagerateHeightScaleEnabled(true),
        _legacyHeightScaleEnabled(false),
        _normalMapLightingShader(),
        _accentColor(Color(0, 0, 0, 255)),
        _shadowColor(Color(0, 0, 0, 255)),
        _highlightColor(Color(255, 255, 255, 255)),
        _illuminationDirection(MapVec(-0.42261826, 0.90630779, -0.70710678)),  // azimuth=335°, altitude=45° (MapLibre defaults)
        // The light stays put as the map rotates: turning the map should not relight the terrain.
        _illuminationMapRotationEnabled(false),
        // IGOR keeps slopes readable under imagery, which is what a hillshade is usually under here.
        _hillshadeMethod(HillshadeMethod::HillshadeMethod::IGOR),
        _contourEnabled(false),
        _elevationEncodingEnabled(false),
        _contourInterval(100.0f),
        _contourColor(Color(0xC5, 0x60, 0x08, 0xff)),
        _contourWidth(0.75f),
        _terrainPaintEnabled(true),
        _terrainPaintFullDetailEnabled(true),
        _paintRotationStep(0)
    {
        setTileBlendingSpeed(0.0f);
    }
    HillshadeRasterTileLayer::HillshadeRasterTileLayer(const std::shared_ptr<TileDataSource> &dataSource) : CustomRasterTileLayer(dataSource),
        _elevationDecoder(nullptr),
        _contrast(0.5f),
        // 0.05, not 1.0: at 1.0 real DEM relief saturates to black and white and the shading reads
        // as a stencil. This is the value the demo and every terrain screenshot here use.
        _heightScale(0.05f),
        _exaggeration(1.0f),
        _exagerateHeightScaleEnabled(true),
        _legacyHeightScaleEnabled(false),
        _normalMapLightingShader(),
        _accentColor(Color(0, 0, 0, 255)),
        _shadowColor(Color(0, 0, 0, 255)),
        _highlightColor(Color(255, 255, 255, 255)),
        _illuminationDirection(MapVec(-0.42261826, 0.90630779, -0.70710678)),  // azimuth=335°, altitude=45° (MapLibre defaults)
        // The light stays put as the map rotates: turning the map should not relight the terrain.
        _illuminationMapRotationEnabled(false),
        // IGOR keeps slopes readable under imagery, which is what a hillshade is usually under here.
        _hillshadeMethod(HillshadeMethod::HillshadeMethod::IGOR),
        _contourEnabled(false),
        _elevationEncodingEnabled(false),
        _contourInterval(100.0f),
        _contourColor(Color(0xC5, 0x60, 0x08, 0xff)),
        _contourWidth(0.75f),
        _terrainPaintEnabled(true),
        _terrainPaintFullDetailEnabled(true),
        _paintRotationStep(0)
    {
        setTileBlendingSpeed(0.0f);
    }

    HillshadeRasterTileLayer::~HillshadeRasterTileLayer()
    {
    }

    float HillshadeRasterTileLayer::getContrast() const
    {
        return _contrast.load();
    }

    void HillshadeRasterTileLayer::setContrast(float contrast) {
        _contrast.store(std::min(1.0f, std::max(0.0f, contrast)));
        updateTiles(false);
    }

    float HillshadeRasterTileLayer::getHeightScale() const {
        return _heightScale.load();
    }

    void HillshadeRasterTileLayer::setHeightScale(float heightScale) {
        _heightScale.store(heightScale);
        updateTiles(false);
    }

    float HillshadeRasterTileLayer::getExaggeration() const {
        return _exaggeration.load();
    }

    void HillshadeRasterTileLayer::setExaggeration(float exaggeration) {
        _exaggeration.store(exaggeration);
        redraw(); // per-frame shader uniform only; no tile re-decode
    }

    Color HillshadeRasterTileLayer::getShadowColor() const {
        return _shadowColor.load();
    }

    void HillshadeRasterTileLayer::setShadowColor(const Color& color) {
        _shadowColor.store(color);
        redraw();
    }
    
    Color HillshadeRasterTileLayer::getAccentColor() const {
        return _accentColor.load();
    }
    
    void HillshadeRasterTileLayer::setAccentColor(const Color &color) {
        _accentColor.store(color);
        redraw();
    }

    Color HillshadeRasterTileLayer::getHighlightColor() const {
        return _highlightColor.load();
    }

    void HillshadeRasterTileLayer::setHighlightColor(const Color& color) {
        _highlightColor.store(color);
        redraw();
    }

    std::string HillshadeRasterTileLayer::getNormalMapLightingShader() const
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _normalMapLightingShader;
    }
    void HillshadeRasterTileLayer::setNormalMapLightingShader(const std::string &shader)
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _normalMapLightingShader = shader;
        redraw();
    }
    MapVec HillshadeRasterTileLayer::getIlluminationDirection() const
    {
        return _illuminationDirection.load();
    }
    void HillshadeRasterTileLayer::setIlluminationDirection(MapVec direction)
    {
        MapVec directionNormalized = direction;
        directionNormalized.normalize();
        _illuminationDirection.store(directionNormalized);
        redraw();
    }
    bool HillshadeRasterTileLayer::getIlluminationMapRotationEnabled() const
    {
        return _illuminationMapRotationEnabled.load();
    }
    void HillshadeRasterTileLayer::setIlluminationMapRotationEnabled(bool enabled)
    {
        _illuminationMapRotationEnabled.store(enabled);
        redraw();
    }
    bool HillshadeRasterTileLayer::getExagerateHeightScaleEnabled() const
    {
        return _exagerateHeightScaleEnabled.load();
    }
    void HillshadeRasterTileLayer::setExagerateHeightScaleEnabled(bool enabled)
    {
        _exagerateHeightScaleEnabled.store(enabled);
        updateTiles(false);
    }
    bool HillshadeRasterTileLayer::isLegacyHeightScaleEnabled() const
    {
        return _legacyHeightScaleEnabled.load();
    }
    void HillshadeRasterTileLayer::setLegacyHeightScaleEnabled(bool enabled)
    {
        _legacyHeightScaleEnabled.store(enabled);
        updateTiles(false);
    }

    HillshadeMethod::HillshadeMethod HillshadeRasterTileLayer::getHillshadeMethod() const {
        return _hillshadeMethod.load();
    }

    void HillshadeRasterTileLayer::setHillshadeMethod(HillshadeMethod::HillshadeMethod method) {
        _hillshadeMethod.store(method);
        redraw();
    }

    bool HillshadeRasterTileLayer::isElevationEncodingEnabled() const {
        return _elevationEncodingEnabled.load();
    }

    void HillshadeRasterTileLayer::setElevationEncodingEnabled(bool enabled) {
        _elevationEncodingEnabled.store(enabled);
        updateTiles(false); // format change (elevation packed into the normal map)
    }

    bool HillshadeRasterTileLayer::isContourEnabled() const {
        return _contourEnabled.load();
    }

    void HillshadeRasterTileLayer::setContourEnabled(bool enabled) {
        _contourEnabled.store(enabled);
        // Toggling contours changes the normal map encoding (elevation packed into B/A), so the
        // tiles must be rebuilt (as with setContrast, which is also baked into the normal map).
        updateTiles(false);
    }

    float HillshadeRasterTileLayer::getContourInterval() const {
        return _contourInterval.load();
    }

    void HillshadeRasterTileLayer::setContourInterval(float interval) {
        _contourInterval.store(interval);
        redraw();
    }

    Color HillshadeRasterTileLayer::getContourColor() const {
        return _contourColor.load();
    }

    void HillshadeRasterTileLayer::setContourColor(const Color& color) {
        _contourColor.store(color);
        redraw();
    }

    float HillshadeRasterTileLayer::getContourWidth() const {
        return _contourWidth.load();
    }

    void HillshadeRasterTileLayer::setContourWidth(float width) {
        _contourWidth.store(width);
        redraw();
    }

    bool HillshadeRasterTileLayer::isTerrainPaintEnabled() const {
        return _terrainPaintEnabled.load();
    }

    void HillshadeRasterTileLayer::setTerrainPaintEnabled(bool enabled) {
        _terrainPaintEnabled.store(enabled);
        updateTiles(false); // the layer switches between having a tile set and having none
    }

    bool HillshadeRasterTileLayer::isTerrainPaintFullDetailEnabled() const {
        return _terrainPaintFullDetailEnabled.load();
    }

    void HillshadeRasterTileLayer::setTerrainPaintFullDetailEnabled(bool enabled) {
        _terrainPaintFullDetailEnabled.store(enabled);
        redraw();
    }

    bool HillshadeRasterTileLayer::isTerrainPaintActive() const {
        if (!_terrainPaintEnabled.load() || isTerrainPaintDisabledByProperty()) {
            return false;
        }
        // Contours no longer disqualify the paint: it grew a contour kind of its own, the same
        // screen-width block the normal-map path uses, computed from the shared DEM in
        // terrainPaintFsh. Asking for contours used to drop the layer back to its own DEM tile set
        // - fetch, decode, normal map, upload and ~5x the render tiles - to draw what the terrain
        // already had on the GPU.
        auto options = getOptions();
        if (!options) {
            return false;
        }
        std::shared_ptr<TerrainOptions> terrainOptions = options->getTerrainOptions();
        // 3D terrain is the whole requirement: the paint shades the elevation texture the terrain
        // has already bound. WITH draped fills it takes its place in the shared bake; WITHOUT them
        // it draws itself as the terrain surface, on the shared ground cover, at its own place in
        // the layer order (GLTileRenderer::renderTerrainPaintSurfaces). Requiring the drape here is
        // what made turning the drape off cost 10 fps: the layer fell back to its own DEM tile set
        // - fetch, decode, normal map, upload, and ~5x the render tiles - to draw what the terrain
        // already had on the GPU.
        if (!terrainOptions || !terrainOptions->isActive()) {
            return false;
        }
        // The paint shades the TERRAIN's elevation texture. Shading it for a layer pointed at a
        // different DEM would quietly show the wrong data, so that configuration keeps its tiles.
        return terrainOptions->getDataSource() == getDataSource();
    }

    void HillshadeRasterTileLayer::loadData(const std::shared_ptr<CullState>& cullState) {
        if (isTerrainPaintActive()) {
            // Nothing to fetch, decode, build a normal map from or upload: the DEM is already on
            // the GPU for the terrain itself. Any tiles from before the switch are dropped, or the
            // renderer would keep drawing them on top of the paint.
            _tileRenderer->refreshTiles(std::vector<std::shared_ptr<TileDrawData> >());
            return;
        }
        CustomRasterTileLayer::loadData(cullState);
    }

    std::size_t HillshadeRasterTileLayer::calculatePaintFingerprint() const {
        std::size_t fingerprint = 0;
        auto mix = [&fingerprint](std::size_t value) {
            fingerprint ^= value + 0x9e3779b9 + (fingerprint << 6) + (fingerprint >> 2);
        };
        auto mixFloat = [&mix](float value) {
            mix(std::hash<float>()(value));
        };
        MapVec illumination = getIlluminationDirection();
        mixFloat(getHeightScale());
        mixFloat(getExaggeration());
        mixFloat(getContrast());
        mixFloat(getOpacity());
        mixFloat(static_cast<float>(illumination.getX()));
        mixFloat(static_cast<float>(illumination.getY()));
        mixFloat(static_cast<float>(illumination.getZ()));
        mix(getShadowColor().getARGB());
        mix(getHighlightColor().getARGB());
        mix(getAccentColor().getARGB());
        mix(static_cast<std::size_t>(getHillshadeMethod()));
        mix(getExagerateHeightScaleEnabled() ? 1 : 2);
        mix(isLegacyHeightScaleEnabled() ? 1 : 2);
        mix(std::hash<std::string>()(getNormalMapLightingShader()));
        // The paint draws the contours itself now, so they are part of its appearance: without
        // them here a contour change leaves every already-baked drape texture in place.
        mixFloat(isContourEnabled() ? getContourInterval() : 0.0f);
        mixFloat(getContourWidth());
        mix(getContourColor().getARGB());
        if (getIlluminationMapRotationEnabled()) {
            mix(static_cast<std::size_t>(_paintRotationStep.load()));
        }
        return fingerprint;
    }

    bool HillshadeRasterTileLayer::paintsEveryDrapeTile() const {
        return isTerrainPaintActive();
    }

    std::size_t HillshadeRasterTileLayer::drapeStackSignature() const {
        std::size_t signature = CustomRasterTileLayer::drapeStackSignature();
        if (isTerrainPaintActive()) {
            // The paint is not made of this layer's tiles, so nothing per-tile can report that it
            // changed: its appearance belongs to the identity of the whole drape stack.
            std::size_t paintFingerprint = calculatePaintFingerprint();
            signature ^= paintFingerprint + 0x9e3779b9 + (signature << 6) + (signature >> 2);
        }
        return signature;
    }

    void HillshadeRasterTileLayer::applyRendererSettings() const {
        _tileRenderer->setNormalMapLightingShader(getNormalMapLightingShader());
        _tileRenderer->setRasterFilterMode(getRasterFilterMode());
        _tileRenderer->setLayerBlendingSpeed(getTileBlendingSpeed());
        _tileRenderer->setNormalMapShadowColor(getShadowColor());
        _tileRenderer->setNormalMapAccentColor(getAccentColor());
        _tileRenderer->setNormalMapHighlightColor(getHighlightColor());
        _tileRenderer->setNormalMapElevationEncoded(isElevationEncoded());
        _tileRenderer->setNormalMapContourInterval(isContourEnabled() ? getContourInterval() : 0.0f);
        _tileRenderer->setNormalMapContourColor(getContourColor());
        _tileRenderer->setNormalMapContourWidth(getContourWidth());
        _tileRenderer->setNormalIlluminationDirection(getIlluminationDirection());
        _tileRenderer->setNormalIlluminationMapRotationEnabled(getIlluminationMapRotationEnabled());

        int hillshadeMethod = 0;
        switch (getHillshadeMethod()) {
            case HillshadeMethod::HillshadeMethod::STANDARD:
                hillshadeMethod = 0;
                break;
            case HillshadeMethod::HillshadeMethod::COMBINED:
                hillshadeMethod = 1;
                break;
            case HillshadeMethod::HillshadeMethod::IGOR:
                hillshadeMethod = 2;
                break;
            case HillshadeMethod::HillshadeMethod::MULTIDIRECTIONAL:
                hillshadeMethod = 3;
                break;
            case HillshadeMethod::HillshadeMethod::BASIC:
                hillshadeMethod = 4;
                break;
        }
        _tileRenderer->setHillshadeMethod(hillshadeMethod);
        // Two separate jobs: exaggeration scales the slope, contrast is MapLibre's
        // 'hillshade-exaggeration' (the slope response curve and the overall strength).
        _tileRenderer->setHillshadeExaggeration(getExaggeration());
        _tileRenderer->setHillshadeIntensity(getContrast());
        // The paint reads the SAME values through the same lighting shader; only where the
        // gradient comes from differs (the terrain DEM instead of a normal map raster).
        // getExaggeration() is applied by the lighting shader itself, so it is not repeated here.
        bool paintActive = isTerrainPaintActive();
        _tileRenderer->setTerrainPaint(paintActive, isTerrainPaintFullDetailEnabled(), getHeightScale(), getExagerateHeightScaleEnabled(), isLegacyHeightScaleEnabled(), getContrast(), getOpacity(), calculatePaintFingerprint());
    }

    bool HillshadeRasterTileLayer::prepareTerrainDrapeFrame(float deltaSeconds, const ViewState& viewState) {
        // The shared drape bakes BEFORE any layer draws, so the paint's parameters have to be on
        // the renderer by now - otherwise the first bake of every tile uses the previous frame's
        // values and, being cached, keeps them. The map rotation is one of those parameters when
        // the illumination follows the map: 2 degree steps, so a rotation gesture re-bakes a
        // bounded number of times instead of once per frame.
        _paintRotationStep.store(static_cast<int>(std::floor(viewState.getRotation() / 2.0f)));
        applyRendererSettings();
        return CustomRasterTileLayer::prepareTerrainDrapeFrame(deltaSeconds, viewState);
    }

    bool HillshadeRasterTileLayer::onDrawFrame(float deltaSeconds, BillboardSorter &billboardSorter, const ViewState &viewState)
    {
        updateTileLoadListener();

        if (auto mapRenderer = getMapRenderer())
        {
            float opacity = getOpacity();

            if (opacity < 1.0f)
            {
                mapRenderer->clearAndBindScreenFBO(Color(0, 0, 0, 0), false, false);
            }

            applyRendererSettings();
            bool refresh = _tileRenderer->onDrawFrame(deltaSeconds, viewState);

            if (opacity < 1.0f)
            {
                mapRenderer->blendAndUnbindScreenFBO(opacity);
            }

            return refresh;
        }
        return false;
    }

    std::shared_ptr<vt::Tile> HillshadeRasterTileLayer::createVectorTile(const MapTile& subTile, const MapTile& tile, const std::shared_ptr<TileData>& tileData, const std::shared_ptr<Bitmap>& bitmap, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const {
        std::uint8_t alpha = 0;
        std::array<float, 4> scales;
        std::array<float, 4> elevationCoeffs = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        {
            std::shared_ptr<ElevationDecoder> decoder = ElevationDecoder::Resolve(tileData, getDataSource(), _elevationDecoder);
            scales = decoder->getVectorTileScales();
            std::array<double, 4> rawCoeffs = decoder->getColorComponentCoefficients();
            elevationCoeffs = { { static_cast<float>(rawCoeffs[0]), static_cast<float>(rawCoeffs[1]), static_cast<float>(rawCoeffs[2]), static_cast<float>(rawCoeffs[3]) } };
            alpha = static_cast<std::uint8_t>(getContrast() * 255.0f);
            float heightScale = decoder->getMinimumHeightScale();
            double zoom = tile.getZoom();
            // Heights are converted into tile-pixel units so the Sobel gradient in NormalMapBuilder
            // comes out as a true slope: bitmapHeight * 2^zoom / earthCircumference = pixels/metre.
            float scale = heightScale * getHeightScale() * static_cast<float>(bitmap->getHeight() * std::pow(2.0, zoom) / 40075016.6855785);
            if (_legacyHeightScaleEnabled) {
                // Pre-MapLibre-parity formula, kept for styles tuned against it. Relief is damped by
                // the ABSOLUTE zoom, so it washes out as you zoom in. Needs heightScale 0.09 to
                // reproduce the old default appearance.
                float exaggeration = zoom < 2 ? 0.2f : zoom < 5 ? 0.3f : 0.35f;
                scale = heightScale * 160 * getHeightScale() * static_cast<float>(bitmap->getHeight() * std::pow(2.0, zoom * (1 - exaggeration)) / 40075016.6855785);
            } else if (_exagerateHeightScaleEnabled && zoom < 15.0) {
                // MapLibre hillshade_prepare.fragment.glsl, verbatim: relief is the true slope from
                // zoom 15 up (15 being the max zoom of Mapbox terrain-RGB, where this constant comes
                // from) and boosted below it, because otherwise it is barely noticeable at low zoom.
                // Unlike the legacy formula it does not flatten as the camera zooms in, which is what
                // keeps the detail on a high-resolution (z15+) DEM.
                float exaggerationFactor = zoom < 2.0 ? 0.4f : zoom < 4.5 ? 0.35f : 0.3f;
                scale *= static_cast<float>(std::pow(2.0, (15.0 - zoom) * exaggerationFactor));
            }
            std::transform(scales.begin(), scales.end(), scales.begin(), [&scale](float &c) { return c * scale; });
        }
        
        // Build normal map from height map
        vt::TileId vtTileId(tile.getZoom(), tile.getX(), tile.getY());
        vt::TileId vtSubTileId(subTile.getZoom(), subTile.getX(), subTile.getY());
        std::shared_ptr<Bitmap> rgbaBitmap = bitmap->getRGBABitmap();
        auto rgbaBitmapDataPtr = reinterpret_cast<const std::uint32_t*>(rgbaBitmap->getPixelData().data());
        std::vector<std::uint32_t> rgbaBitmapData(rgbaBitmapDataPtr, rgbaBitmapDataPtr + rgbaBitmap->getWidth() * rgbaBitmap->getHeight());
        auto vtBitmap = std::make_shared<vt::Bitmap>(rgbaBitmap->getWidth(), rgbaBitmap->getHeight(), std::move(rgbaBitmapData));
        vt::NormalMapBuilder normalMapBuilder(scales, alpha, isElevationEncoded(), elevationCoeffs);
        std::shared_ptr<const vt::Bitmap> normalMap = normalMapBuilder.buildNormalMapFromHeightMap(vtTileId, vtTileId, vtBitmap);
        auto normalMapDataPtr = reinterpret_cast<const std::uint8_t*>(normalMap->data.data());
        std::vector<std::uint8_t> normalMapData(normalMapDataPtr, normalMapDataPtr + normalMap->data.size() * sizeof(std::uint32_t));
        auto tileBitmap = std::make_shared<vt::TileBitmap>(vt::TileBitmap::Type::NORMALMAP, vt::TileBitmap::Format::RGBA, normalMap->width, normalMap->height, std::move(normalMapData));
        
        // Build vector tile from created normal map
        float tileSize = 256.0f; // 'normalized' tile size in pixels. Not really important
        vt::TileLayerBuilder tileLayerBuilder(std::string(), 0, vtTileId, tileTransformer, tileSize, 1.0f); // Note: the size/scale argument is ignored
        tileLayerBuilder.addBitmap(tileBitmap);
        std::shared_ptr<vt::TileLayer> tileLayer = tileLayerBuilder.buildTileLayer();
        return std::make_shared<vt::Tile>(vtTileId, tileSize, std::vector<std::shared_ptr<vt::TileLayer> > { tileLayer });
    }

    std::shared_ptr<Bitmap> HillshadeRasterTileLayer::getTileDataBitmap(std::shared_ptr<TileData> tileData) const {
        std::shared_ptr<BinaryData> binaryData = tileData->getData();
        if (!binaryData) {
            Log::Error("HillshadeRasterTileLayer::getTileDataBitmap: Null tile binary data");
            return NULL;
        }
        int size = binaryData->size();
        std::shared_ptr<Bitmap> tileBitmap = Bitmap::CreateFromCompressed(binaryData);
        return tileBitmap;
    }

    std::shared_ptr<ElevationManager> HillshadeRasterTileLayer::getElevationManager() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_elevationManager) {
            _elevationManager = std::make_shared<ElevationManager>(getDataSource(), _elevationDecoder);
        }
        return _elevationManager;
    }

    double HillshadeRasterTileLayer::getElevation(const MapPos &pos) const
    {
        return getElevationManager()->getElevation(pos);
    }

    std::vector<double> HillshadeRasterTileLayer::getElevations(const std::vector<MapPos> poses) const
    {
        return getElevationManager()->getElevations(poses);
    }
} // namespace massif
