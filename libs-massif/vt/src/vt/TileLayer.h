/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILELAYER_H_
#define _CARTO_VT_TILELAYER_H_

#include "TileBackground.h"
#include "TileBitmap.h"
#include "TileGeometry.h"
#include "TileLabel.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <numeric>

namespace carto { namespace vt {
    class TileLayer final {
    public:
        explicit TileLayer(std::string layerName, int layerIdx, std::optional<CompOp> compOp, FloatFunction opacityFunc, std::vector<std::shared_ptr<TileBackground>> backgrounds, std::vector<std::shared_ptr<TileBitmap>> bitmaps, std::vector<std::shared_ptr<TileGeometry>> geometries, std::vector<std::shared_ptr<TileLabel>> labels) : _layerName(std::move(layerName)), _layerIdx(layerIdx), _compOp(std::move(compOp)), _opacityFunc(std::move(opacityFunc)), _backgrounds(std::move(backgrounds)), _bitmaps(std::move(bitmaps)), _geometries(std::move(geometries)), _labels(std::move(labels)) { }

        const std::string& getLayerName() const { return _layerName; }
        int getLayerIndex() const { return _layerIdx; }
        const std::optional<CompOp>& getCompOp() const { return _compOp; }
        const FloatFunction& getOpacityFunc() const { return _opacityFunc; }

        const std::vector<std::shared_ptr<TileBackground>>& getBackgrounds() const { return _backgrounds; }
        const std::vector<std::shared_ptr<TileBitmap>>& getBitmaps() const { return _bitmaps; }
        const std::vector<std::shared_ptr<TileGeometry>>& getGeometries() const { return _geometries; }
        const std::vector<std::shared_ptr<TileLabel>>& getLabels() const { return _labels; }

        std::size_t getFeatureCount() const {
            std::size_t featureCount = std::accumulate(_geometries.begin(), _geometries.end(), static_cast<std::size_t>(0), [](std::size_t count, const std::shared_ptr<TileGeometry>& geometry) { return count + geometry->getFeatureCount(); });
            featureCount += _labels.size();
            return featureCount;
        }

        std::size_t getResidentSize() const {
            std::size_t backgroundsSize = std::accumulate(_backgrounds.begin(), _backgrounds.end(), static_cast<std::size_t>(0), [](std::size_t size, const std::shared_ptr<TileBackground>& background) { return size + background->getResidentSize(); });
            std::size_t bitmapsSize = std::accumulate(_bitmaps.begin(), _bitmaps.end(), static_cast<std::size_t>(0), [](std::size_t size, const std::shared_ptr<TileBitmap>& bitmap) { return size + bitmap->getResidentSize(); });
            std::size_t geometriesSize = std::accumulate(_geometries.begin(), _geometries.end(), static_cast<std::size_t>(0), [](std::size_t size, const std::shared_ptr<TileGeometry>& geometry) { return size + geometry->getResidentSize(); });
            std::size_t labelsSize = std::accumulate(_labels.begin(), _labels.end(), static_cast<std::size_t>(0), [](std::size_t size, const std::shared_ptr<TileLabel>& label) { return size + label->getResidentSize(); });
            return 16 + backgroundsSize + bitmapsSize + geometriesSize + labelsSize;
        }

    private:
        const std::string _layerName;
        const int _layerIdx;
        const std::optional<CompOp> _compOp;
        const FloatFunction _opacityFunc;
        const std::vector<std::shared_ptr<TileBackground>> _backgrounds;
        const std::vector<std::shared_ptr<TileBitmap>> _bitmaps;
        const std::vector<std::shared_ptr<TileGeometry>> _geometries;
        const std::vector<std::shared_ptr<TileLabel>> _labels;
    };
} }

#endif
