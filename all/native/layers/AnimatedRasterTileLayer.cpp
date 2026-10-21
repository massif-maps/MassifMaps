#include "AnimatedRasterTileLayer.h"
#include "components/Exceptions.h"
#include "core/BinaryData.h"
#include "graphics/Bitmap.h"
#include "datasources/CacheTileDataSource.h"
#include "datasources/HTTPTileDataSource.h"
#ifdef _MASSIF_OFFLINE_SUPPORT
#include "datasources/PersistentCacheTileDataSource.h"
#endif
#include "utils/FrameTimestamps.h"
#include "utils/PrefetchPlan.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace massif {

    AnimatedRasterTileLayer::AnimatedRasterTileLayer(const std::shared_ptr<TileDataSource>& dataSource) :
        CustomRasterTileLayer(dataSource),
        _timestamps(),
        _frameNrs(),
        _position(0.0f),
        _prefetchFrames(2),
        _prefetchBudget(DEFAULT_PREFETCH_BUDGET_MB),
        _stepBitmaps(),
        _inFlight(),
        _inFlightMutex(),
        _prefetchCacheBytes(0),
        _placeholder(),
        _animationMutex()
    {
    }

    AnimatedRasterTileLayer::~AnimatedRasterTileLayer() {
    }

    std::vector<std::string> AnimatedRasterTileLayer::getFrameTimestamps() const {
        std::lock_guard<std::mutex> lock(_animationMutex);
        return _timestamps;
    }

    void AnimatedRasterTileLayer::setFrameTimestamps(const std::vector<std::string>& timestamps) {
        if (timestamps.size() > MAX_FRAME_COUNT) {
            throw InvalidArgumentException("At most " + std::to_string(MAX_FRAME_COUNT) + " time steps");
        }
        bool timed = false;
        for (std::shared_ptr<TileDataSource> source = getDataSource(); source && !timed;) {
            if (auto httpSource = std::dynamic_pointer_cast<HTTPTileDataSource>(source)) {
                httpSource->setFrameTimestamps(timestamps);
                timed = true;
            }
#ifdef _MASSIF_OFFLINE_SUPPORT
            if (auto persistentSource = std::dynamic_pointer_cast<PersistentCacheTileDataSource>(source)) {
                persistentSource->purgeExpired();
            }
#endif
            auto cacheSource = std::dynamic_pointer_cast<CacheTileDataSource>(source);
            source = cacheSource ? cacheSource->getDataSource() : nullptr;
        }
        if (!timed) {
            Log::Warn("AnimatedRasterTileLayer: no HTTPTileDataSource behind the layer's source, so {time} is not filled");
        }
        {
            std::lock_guard<std::mutex> lock(_animationMutex);
            _timestamps = timestamps;
            _frameNrs = AssignFrameNrs(timestamps);
        }
        setAnimationPosition(getAnimationPosition());
    }

    float AnimatedRasterTileLayer::getAnimationPosition() const {
        std::lock_guard<std::mutex> lock(_animationMutex);
        return _position;
    }

    void AnimatedRasterTileLayer::setAnimationPosition(float position) {
        {
            std::lock_guard<std::mutex> lock(_animationMutex);
            _position = position;
        }
        int step = 0;
        float fraction = 0.0f;
        int frameNr = 0;
        bool hasFrames = positionState(step, fraction, frameNr);
        std::map<std::string, Variant> parameters;
        parameters.emplace("u_step", Variant(static_cast<double>(step)));
        parameters.emplace("u_fraction", Variant(static_cast<double>(fraction)));
        setShaderParameters(parameters);
        // Not under _animationMutex: the layer's own lock is taken first by the cull, which calls back into this class.
        if (hasFrames && getFrameNr() != frameNr) {
            setFrameNr(frameNr);
        }
    }

    int AnimatedRasterTileLayer::getPrefetchFrames() const {
        std::lock_guard<std::mutex> lock(_animationMutex);
        return _prefetchFrames;
    }

    void AnimatedRasterTileLayer::setPrefetchFrames(int frames) {
        {
            std::lock_guard<std::mutex> lock(_animationMutex);
            _prefetchFrames = std::min(std::max(frames, 0), static_cast<int>(MAX_FRAME_COUNT));
        }
        refresh();
    }

    int AnimatedRasterTileLayer::getPrefetchBudget() const {
        std::lock_guard<std::mutex> lock(_animationMutex);
        return _prefetchBudget;
    }

    void AnimatedRasterTileLayer::setPrefetchBudget(int megabytes) {
        {
            std::lock_guard<std::mutex> lock(_animationMutex);
            _prefetchBudget = std::min(std::max(megabytes, 16), 1024);
        }
        refresh();
    }

    bool AnimatedRasterTileLayer::positionState(int& step, float& fraction, int& frameNr) const {
        std::lock_guard<std::mutex> lock(_animationMutex);
        if (_frameNrs.empty()) {
            step = 0;
            fraction = 0.0f;
            frameNr = 0;
            return false;
        }
        float position = std::min(std::max(_position, 0.0f), static_cast<float>(_frameNrs.size() - 1));
        step = static_cast<int>(std::floor(position));
        fraction = step + 1 >= static_cast<int>(_frameNrs.size()) ? 0.0f : position - static_cast<float>(step);
        frameNr = _frameNrs[step];
        return true;
    }

    std::string AnimatedRasterTileLayer::getEffectiveShaderSource() const {
        std::lock_guard<std::recursive_mutex> lock(_customShaderMutex);
        return SHADER_HELPERS + (_shaderSource.empty() ? MIX_SHADER : _shaderSource);
    }

    std::shared_ptr<vt::TileBitmap> AnimatedRasterTileLayer::stepBitmap(int step) const {
        if (std::shared_ptr<vt::TileBitmap> bitmap = _stepBitmaps.find(step)) {
            return bitmap;
        }
        std::vector<std::uint8_t> texel { static_cast<std::uint8_t>(step), 0, 0, 255 };
        return _stepBitmaps.share(step, std::make_shared<vt::TileBitmap>(vt::TileBitmap::Type::NORMALMAP, vt::TileBitmap::Format::RGBA, 1, 1, std::move(texel)));
    }

    bool AnimatedRasterTileLayer::hasStep(const MapTile& tile) const {
        std::lock_guard<std::mutex> lock(_animationMutex);
        return std::find(_frameNrs.begin(), _frameNrs.end(), tile.getFrameNr()) != _frameNrs.end();
    }

    std::shared_ptr<TileData> AnimatedRasterTileLayer::loadTileData(const std::shared_ptr<TileDataSource>& dataSource, const MapTile& tile) const {
        auto key = std::make_pair(dataSource.get(), tile.getTileId());
        std::promise<std::shared_ptr<TileData> > promise;
        std::shared_future<std::shared_ptr<TileData> > future;
        bool loader = false;
        {
            std::lock_guard<std::mutex> lock(_inFlightMutex);
            auto it = _inFlight.find(key);
            if (it != _inFlight.end()) {
                future = it->second;
            } else {
                future = promise.get_future().share();
                _inFlight.emplace(key, future);
                loader = true;
            }
        }
        if (!loader) {
            return future.get();
        }
        std::shared_ptr<TileData> tileData;
        try {
            tileData = dataSource->loadTile(tile);
        } catch (...) {
            promise.set_value(nullptr);
            std::lock_guard<std::mutex> lock(_inFlightMutex);
            _inFlight.erase(key);
            throw;
        }
        promise.set_value(tileData);
        std::lock_guard<std::mutex> lock(_inFlightMutex);
        _inFlight.erase(key);
        return tileData;
    }

    std::shared_ptr<Bitmap> AnimatedRasterTileLayer::decodeTileData(const MapTile& tile, const std::shared_ptr<TileData>& tileData) const {
        if (isSharedBitmap(tileData) && hasStep(tile)) {
            // The tile reuses the live bitmap of the same bytes, so this decode would be thrown away.
            std::lock_guard<std::mutex> lock(_inFlightMutex);
            if (!_placeholder) {
                _placeholder = std::make_shared<Bitmap>(std::make_shared<BinaryData>(std::vector<unsigned char>(4, 0)), 1, 1, ColorFormat::COLOR_FORMAT_RGBA, 4);
            }
            return _placeholder;
        }
        return CustomRasterTileLayer::decodeTileData(tile, tileData);
    }

    bool AnimatedRasterTileLayer::loadExtraBitmaps(const MapTile& dataSourceTile, const CancelableTask& task, std::vector<std::shared_ptr<vt::TileBitmap> >& extraBitmaps) const {
        std::vector<std::shared_ptr<vt::TileBitmap> > next;
        bool complete = CustomRasterTileLayer::loadExtraBitmaps(dataSourceTile, task, next);
        int step = -1;
        {
            std::lock_guard<std::mutex> lock(_animationMutex);
            auto it = std::find(_frameNrs.begin(), _frameNrs.end(), dataSourceTile.getFrameNr());
            if (it != _frameNrs.end()) {
                step = static_cast<int>(it - _frameNrs.begin());
            }
        }
        if (step >= 0) {
            // Slot 1 names the tile's own step, so a tile that is swapped in late still mixes by the right amount.
            extraBitmaps.push_back(stepBitmap(step));
        }
        extraBitmaps.insert(extraBitmaps.end(), next.begin(), next.end());
        return complete;
    }

    std::vector<CustomRasterTileLayer::Input> AnimatedRasterTileLayer::getExtraInputs(const MapTile& dataSourceTile) const {
        std::lock_guard<std::mutex> lock(_animationMutex);
        auto it = std::find(_frameNrs.begin(), _frameNrs.end(), dataSourceTile.getFrameNr());
        if (it == _frameNrs.end()) {
            return std::vector<Input>();
        }
        std::size_t next = std::min(static_cast<std::size_t>(it - _frameNrs.begin()) + 1, _frameNrs.size() - 1);
        return { { getDataSource(), MapTile(dataSourceTile.getX(), dataSourceTile.getY(), dataSourceTile.getZoom(), _frameNrs[next]) } };
    }

    std::vector<MapTile> AnimatedRasterTileLayer::collectFramePrefetchTiles(const std::vector<MapTile>& visibleTiles) {
        std::vector<MapTile> tiles;
        std::size_t capacity = 0;
        int currentFrameNr = getFrameNr();
        {
            std::lock_guard<std::mutex> lock(_animationMutex);
            auto it = std::find(_frameNrs.begin(), _frameNrs.end(), currentFrameNr);
            if (it == _frameNrs.end() || visibleTiles.empty()) {
                return tiles;
            }
            std::size_t step = it - _frameNrs.begin();
            PrefetchPlan plan = PlanPrefetch(visibleTiles.size(), static_cast<std::size_t>(_prefetchFrames), _frameNrs.size(), TILE_BYTES, static_cast<std::size_t>(_prefetchBudget) * 1024 * 1024, MIN_CACHE_BYTES);
            std::size_t count = plan.frames;
            if (plan.cacheBytes > _prefetchCacheBytes) {
                _prefetchCacheBytes = plan.cacheBytes + plan.cacheBytes / 4;
                capacity = _prefetchCacheBytes;
            }
            // Wraps: a loop that ends comes round to its first step again.
            for (std::size_t ahead = 1; ahead <= count; ahead++) {
                for (const MapTile& tile : visibleTiles) {
                    tiles.emplace_back(tile.getX(), tile.getY(), tile.getZoom(), _frameNrs[(step + ahead) % _frameNrs.size()]);
                }
            }
        }
        if (capacity > 0) {
            // Not under _animationMutex: the layer's own lock comes first.
            setTextureCacheCapacity(capacity);
        }
        return tiles;
    }

    const std::string AnimatedRasterTileLayer::SHADER_HELPERS =
        "uniform float u_step;\n"
        "uniform float u_fraction;\n"
        "vec4 stepFrom() { return sampleRaster(0); }\n"
        "vec4 stepTo() { return sampleRaster(2); }\n"
        "float stepMix() {\n"
        "    float tileStep = floor(sampleRaster(1).r * 255.0 + 0.5);\n"
        "    return clamp(u_step - tileStep + u_fraction, 0.0, 1.0);\n"
        "}\n";

    const std::string AnimatedRasterTileLayer::MIX_SHADER =
        "vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {\n"
        "    return mix(stepFrom(), stepTo(), stepMix());\n"
        "}\n";

}
