#include "CustomRasterTileLayer.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "graphics/Bitmap.h"
#include "graphics/Color.h"

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

    std::shared_ptr<vt::Tile> CustomRasterTileLayer::createVectorTile(const MapTile& subTile, const MapTile& tile, const std::shared_ptr<TileData>& tileData, const std::shared_ptr<Bitmap>& bitmap, const std::shared_ptr<vt::TileTransformer>& tileTransformer) const {
        // Upload the raw RGBA tile as a NORMALMAP-type bitmap so it runs through the custom fragment
        // shader path (the injected shader samples it via getRawColor()). No normal map is computed.
        vt::TileId vtTileId(tile.getZoom(), tile.getX(), tile.getY());
        std::shared_ptr<Bitmap> rgbaBitmap = bitmap->getRGBABitmap();
        int width = rgbaBitmap->getWidth();
        int height = rgbaBitmap->getHeight();
        const std::vector<unsigned char>& pixelData = rgbaBitmap->getPixelData();
        std::vector<std::uint8_t> data(pixelData.begin(), pixelData.end());
        auto tileBitmap = std::make_shared<vt::TileBitmap>(vt::TileBitmap::Type::NORMALMAP, vt::TileBitmap::Format::RGBA, width, height, std::move(data));

        float tileSize = 256.0f; // 'normalized' tile size in pixels; not important here
        vt::TileLayerBuilder tileLayerBuilder(std::string(), 0, vtTileId, tileTransformer, tileSize, 1.0f);
        tileLayerBuilder.addBitmap(tileBitmap);
        std::shared_ptr<vt::TileLayer> tileLayer = tileLayerBuilder.buildTileLayer();
        return std::make_shared<vt::Tile>(vtTileId, tileSize, std::vector<std::shared_ptr<vt::TileLayer> > { tileLayer });
    }

    const std::string CustomRasterTileLayer::PASSTHROUGH_SHADER =
        "vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {\n"
        "    return getRawColor();\n"
        "}\n";

}
