#include "CustomRasterTileLayer.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "graphics/Bitmap.h"
#include "graphics/Color.h"
#include "components/Exceptions.h"
#include "core/BinaryData.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileBitmap.h"

#include <algorithm>
#include <string_view>

#include <vt/RasterGroups.h>
#include <vt/TileId.h>
#include <vt/Tile.h>
#include <vt/TileBitmap.h>
#include <vt/TileLayer.h>
#include <vt/TileLayerBuilder.h>
#include <vt/TileTransformer.h>

namespace massif {

    CustomRasterTileLayer::CustomRasterTileLayer(const std::shared_ptr<TileDataSource>& dataSource) :
        RasterTileLayer(dataSource),
        _shaderSource(),
        _publishedDrapeHash(0),
        _publishedDrapeAt(),
        _customShaderMutex()
    {
        setTileBlendingSpeed(0.0f);
    }

    CustomRasterTileLayer::~CustomRasterTileLayer() {
    }

    std::string CustomRasterTileLayer::getShaderSource() const {
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        return _shaderSource;
    }

    void CustomRasterTileLayer::setShaderSource(const std::string& shaderSource) {
        {
            std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
            _shaderSource = shaderSource;
        }
        redraw();
    }

    std::string CustomRasterTileLayer::getEffectiveShaderSource() const {
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        return _shaderSource.empty() ? PASSTHROUGH_SHADER : _shaderSource;
    }

    void CustomRasterTileLayer::addDataSource(const std::shared_ptr<TileDataSource>& dataSource) {
        if (!dataSource) {
            throw NullArgumentException("Null dataSource");
        }
        {
            std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
            if (_extraDataSources.size() + 1 >= static_cast<std::size_t>(vt::MAX_RASTER_SLOTS)) {
                throw InvalidArgumentException("At most " + std::to_string(vt::MAX_RASTER_SLOTS) + " data sources");
            }
            bool listened = dataSource == getDataSource() || std::find(_extraDataSources.begin(), _extraDataSources.end(), dataSource) != _extraDataSources.end();
            _extraDataSources.push_back(dataSource);
            if (_dataSourceListener && !listened) {
                dataSource->registerOnChangeListener(_dataSourceListener);
            }
        }
        updateTiles(true);
    }

    int CustomRasterTileLayer::getDataSourceCount() const {
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        return static_cast<int>(_extraDataSources.size()) + 1;
    }

    void CustomRasterTileLayer::setShaderParameter(const std::string& name, float value) {
        {
            std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
            _shaderParameters[name] = value;
            _tileRenderer->setNormalMapShaderParameters(_shaderParameters);
        }
        redraw();
    }

    float CustomRasterTileLayer::getShaderParameter(const std::string& name) const {
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        auto it = _shaderParameters.find(name);
        return it == _shaderParameters.end() ? 0.0f : it->second;
    }

    std::map<std::string, Variant> CustomRasterTileLayer::getShaderParameters() const {
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        std::map<std::string, Variant> parameters;
        for (const auto& parameter : _shaderParameters) {
            parameters.emplace(parameter.first, Variant(static_cast<double>(parameter.second)));
        }
        return parameters;
    }

    void CustomRasterTileLayer::setShaderParameters(const std::map<std::string, Variant>& parameters) {
        {
            std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
            for (const auto& parameter : parameters) {
                _shaderParameters[parameter.first] = static_cast<float>(parameter.second.getDouble());
            }
            _tileRenderer->setNormalMapShaderParameters(_shaderParameters);
        }
        redraw();
    }

    void CustomRasterTileLayer::registerDataSourceListener() {
        RasterTileLayer::registerDataSourceListener();
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        for (const std::shared_ptr<TileDataSource>& dataSource : _extraDataSources) {
            if (dataSource != getDataSource()) {
                dataSource->registerOnChangeListener(_dataSourceListener);
            }
        }
    }

    void CustomRasterTileLayer::unregisterDataSourceListener() {
        {
            std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
            for (const std::shared_ptr<TileDataSource>& dataSource : _extraDataSources) {
                dataSource->unregisterOnChangeListener(_dataSourceListener);
            }
        }
        RasterTileLayer::unregisterDataSourceListener();
    }

    std::size_t CustomRasterTileLayer::drapeStackSignature() const {
        std::size_t signature = RasterTileLayer::drapeStackSignature();
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        auto mix = [](std::size_t& hash, std::size_t value) {
            hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        };
        std::size_t live = 0;
        if (!_shaderSource.empty()) {
            mix(live, std::hash<std::string>()(_shaderSource));
        }
        for (const auto& parameter : _shaderParameters) {
            mix(live, std::hash<std::string>()(parameter.first));
            mix(live, std::hash<float>()(parameter.second));
        }
        if (live != _publishedDrapeHash) {
            auto now = std::chrono::steady_clock::now();
            if (_publishedDrapeAt == std::chrono::steady_clock::time_point() || now - _publishedDrapeAt >= std::chrono::milliseconds(DRAPE_REBAKE_MIN_MS)) {
                _publishedDrapeHash = live;
                _publishedDrapeAt = now;
            } else {
                redraw(); // a still map has no other frame to publish it on
            }
        }
        if (_publishedDrapeHash != 0) {
            mix(signature, _publishedDrapeHash);
        }
        return signature;
    }

    std::vector<CustomRasterTileLayer::Input> CustomRasterTileLayer::getExtraInputs(const MapTile& dataSourceTile) const {
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        std::vector<Input> inputs;
        for (const std::shared_ptr<TileDataSource>& dataSource : _extraDataSources) {
            inputs.push_back({ dataSource, dataSourceTile });
        }
        return inputs;
    }

    bool CustomRasterTileLayer::loadExtraBitmaps(const MapTile& dataSourceTile, const CancelableTask& task, std::vector<std::shared_ptr<vt::TileBitmap>>& extraBitmaps) const {
        for (const Input& input : getExtraInputs(dataSourceTile)) {
            if (task.isCanceled()) {
                return false;
            }
            std::shared_ptr<TileData> tileData = loadTileData(input.dataSource, input.tile);
            if (!tileData || tileData->isReplaceWithParent()) {
                return false;
            }
            ContentKey key = MakeContentKey(tileData);
            std::shared_ptr<vt::TileBitmap> tileBitmap = _sharedBitmaps.find(key);
            if (!tileBitmap) {
                std::shared_ptr<Bitmap> bitmap = DecodeTileBitmap(tileData);
                if (!bitmap) {
                    return false;
                }
                tileBitmap = _sharedBitmaps.share(key, MakeTileBitmap(bitmap));
            }
            extraBitmaps.push_back(tileBitmap);
        }
        return true;
    }

    bool CustomRasterTileLayer::onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState) {
        if (!isVisible()) {
            return false;
        }

        updateTileLoadListener();

        if (auto mapRenderer = getMapRenderer()) {
            float opacity = getOpacity();

            if (opacity < 1.0f) {
                mapRenderer->clearAndBindScreenFBO(Color(0, 0, 0, 0), false, false);
            }

            _tileRenderer->setNormalMapLightingShader(getEffectiveShaderSource());
            _tileRenderer->setRasterFilterMode(getRasterFilterMode());
            _tileRenderer->setLayerBlendingSpeed(getTileBlendingSpeed());
            // Generic raster filter: no DEM elevation encoding or built-in contours.
            _tileRenderer->setNormalMapElevationEncoded(false);
            _tileRenderer->setNormalMapContourInterval(0.0f);

            bool refresh = _tileRenderer->onDrawFrame(deltaSeconds, viewState);

            if (opacity < 1.0f) {
                mapRenderer->blendAndUnbindScreenFBO(opacity);
            }

            return refresh;
        }
        return false;
    }

    bool CustomRasterTileLayer::isSharedBitmap(const std::shared_ptr<TileData>& tileData) const {
        return tileData && _sharedBitmaps.find(MakeContentKey(tileData)) != nullptr;
    }

    CustomRasterTileLayer::ContentKey CustomRasterTileLayer::MakeContentKey(const std::shared_ptr<TileData>& tileData) {
        std::shared_ptr<BinaryData> data = tileData->getData();
        if (!data) {
            return ContentKey(0, 0);
        }
        std::size_t hash = std::hash<std::string_view>()(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
        hash ^= std::hash<int>()(tileData->getWidth()) * 31 + std::hash<int>()(tileData->getHeight());
        return ContentKey(hash, data->size());
    }

    std::shared_ptr<vt::TileBitmap> CustomRasterTileLayer::MakeTileBitmap(const std::shared_ptr<Bitmap>& bitmap) {
        // Upload the raw RGBA tile as a NORMALMAP-type bitmap so it runs through the custom fragment
        // shader path (the injected shader samples it via getRawColor()). No normal map is computed.
        std::shared_ptr<Bitmap> rgbaBitmap = bitmap->getRGBABitmap();
        const std::vector<unsigned char>& pixelData = rgbaBitmap->getPixelData();
        std::vector<std::uint8_t> data(pixelData.begin(), pixelData.end());
        return std::make_shared<vt::TileBitmap>(vt::TileBitmap::Type::NORMALMAP, vt::TileBitmap::Format::RGBA, rgbaBitmap->getWidth(), rgbaBitmap->getHeight(), std::move(data));
    }

    std::shared_ptr<vt::Tile> CustomRasterTileLayer::buildTile(const MapTile& tile, const std::vector<std::shared_ptr<vt::TileBitmap>>& bitmaps, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const {
        vt::TileId vtTileId(tile.getZoom(), tile.getX(), tile.getY());
        float tileSize = 256.0f; // 'normalized' tile size in pixels; not important here
        vt::TileLayerBuilder tileLayerBuilder(std::string(), 0, vtTileId, tileTransformer, tileSize, 1.0f);
        for (const std::shared_ptr<vt::TileBitmap>& bitmap : bitmaps) {
            tileLayerBuilder.addBitmap(bitmap);
        }
        std::shared_ptr<vt::TileLayer> tileLayer = tileLayerBuilder.buildTileLayer();
        return std::make_shared<vt::Tile>(vtTileId, tileSize, std::vector<std::shared_ptr<vt::TileLayer> > { tileLayer });
    }

    std::shared_ptr<vt::Tile> CustomRasterTileLayer::createVectorTile(const MapTile& subTile, const MapTile& tile, const std::shared_ptr<TileData>& tileData, const std::shared_ptr<Bitmap>& bitmap, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const {
        return buildTile(tile, { MakeTileBitmap(bitmap) }, tileTransformer);
    }

    std::shared_ptr<vt::Tile> CustomRasterTileLayer::createVectorTile(const MapTile& subTile, const MapTile& tile, const std::shared_ptr<TileData>& tileData, const std::shared_ptr<Bitmap>& bitmap, const std::vector<std::shared_ptr<vt::TileBitmap>>& extraBitmaps, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const {
        // Hillshade, and a layer with a single source, keep the one-bitmap path through the virtual above.
        if (extraBitmaps.empty() && getExtraInputs(tile).empty()) {
            return createVectorTile(subTile, tile, tileData, bitmap, tileTransformer);
        }
        ContentKey key = MakeContentKey(tileData);
        std::shared_ptr<vt::TileBitmap> primary = _sharedBitmaps.find(key);
        if (!primary) {
            primary = _sharedBitmaps.share(key, MakeTileBitmap(bitmap));
        }
        std::vector<std::shared_ptr<vt::TileBitmap> > bitmaps { primary };
        bitmaps.insert(bitmaps.end(), extraBitmaps.begin(), extraBitmaps.end());
        return buildTile(tile, bitmaps, tileTransformer);
    }

    const std::string CustomRasterTileLayer::PASSTHROUGH_SHADER =
        "vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {\n"
        "    return getRawColor();\n"
        "}\n";

}
