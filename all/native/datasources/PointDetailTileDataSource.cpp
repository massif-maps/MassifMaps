#include "PointDetailTileDataSource.h"
#include "core/BinaryData.h"
#include "core/MapBounds.h"
#include "core/MapPos.h"
#include "core/MapTile.h"
#include "components/Exceptions.h"
#include "projections/Projection.h"
#include "utils/TileUtils.h"
#include "utils/Log.h"
#include "vectortiles/utils/MVTLogger.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include <mapnikvt/Feature.h>
#include <mapnikvt/Geometry.h>
#include <mapnikvt/MBVTFeatureDecoder.h>
#include <mapnikvt/Value.h>

#include <mbvtbuilder/MBVTTileBuilder.h>

namespace massif {

    namespace {

        /**
         * One point, in WGS84, with its properties in their original type (an integer `ele` must stay
         * one or `[ele]+'m'` prints a decimal tail).
         */
        struct PointFeature {
            double lon = 0;
            double lat = 0;
            double rank = 0;
            long long id = 0;
            picojson::object properties;
        };

        /** The rank a feature sorts by, or 0 where the property is missing or not a number. */
        double readRank(const mvt::Value& value) {
            if (auto intValue = std::get_if<long long>(&value)) {
                return static_cast<double>(*intValue);
            }
            if (auto doubleValue = std::get_if<double>(&value)) {
                return *doubleValue;
            }
            if (auto stringValue = std::get_if<std::string>(&value)) {
                // Text-typed elevations still rank: the cap depends on this property.
                try {
                    return std::stod(*stringValue);
                } catch (const std::exception&) {
                    return 0;
                }
            }
            return 0;
        }

        /**
         * Every point of one layer in one tile, appended, in WGS84.
         */
        void collectTile(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<Projection>& projection, const MapTile& detailTile,
                         const std::string& layerName, const std::string& rankProperty, std::vector<PointFeature>& features) {
            std::shared_ptr<TileData> tileData = dataSource->loadTile(detailTile);
            if (!tileData || tileData->isReplaceWithParent() || !tileData->getData() || tileData->getData()->empty()) {
                return;
            }

            // No transform or clip box: geometry arrives in the tile's unit square, which the bounds below span.
            mvt::MBVTFeatureDecoder decoder(*tileData->getData()->getDataPtr(), std::make_shared<MVTLogger>("PointDetailTileDataSource"));
            if (!decoder.hasLayer(layerName)) {
                return;
            }
            std::shared_ptr<mvt::FeatureDecoder::FeatureIterator> it = decoder.createLayerFeatureIterator(layerName, nullptr, false);
            if (!it) {
                return;
            }

            // Flipped: CalculateMapTileOrigin counts Y up from the south, MapTile's Y counts down.
            MapBounds bounds = TileUtils::CalculateMapTileBounds(detailTile.getFlipped(), projection);
            double originX = bounds.getMin().getX();
            double originY = bounds.getMax().getY();
            double width = bounds.getMax().getX() - bounds.getMin().getX();
            double height = bounds.getMax().getY() - bounds.getMin().getY();

            for (; it->valid(); it->advance()) {
                std::shared_ptr<const mvt::Geometry> geometry = it->getGeometry();
                if (!geometry) {
                    continue;
                }
                auto pointGeometry = std::get_if<mvt::PointGeometry>(geometry.get());
                if (!pointGeometry) {
                    continue; // lines and polygons would need clipping
                }
                std::shared_ptr<const mvt::FeatureData> featureData = it->getFeatureData(false, nullptr);
                if (!featureData) {
                    continue;
                }

                picojson::object properties;
                double rank = 0;
                for (const std::pair<std::string, mvt::Value>& variable : featureData->getVariables()) {
                    if (auto stringValue = std::get_if<std::string>(&variable.second)) {
                        properties[variable.first] = picojson::value(*stringValue);
                    } else if (auto intValue = std::get_if<long long>(&variable.second)) {
                        properties[variable.first] = picojson::value(static_cast<std::int64_t>(*intValue));
                    } else if (auto doubleValue = std::get_if<double>(&variable.second)) {
                        properties[variable.first] = picojson::value(*doubleValue);
                    } else if (auto boolValue = std::get_if<bool>(&variable.second)) {
                        properties[variable.first] = picojson::value(*boolValue);
                    }
                    // MBVT carries scalars only.
                    if (variable.first == rankProperty) {
                        rank = readRank(variable.second);
                    }
                }

                long long featureId = it->getFeatureId();
                for (const cglib::vec2<float>& vertex : pointGeometry->getVertices()) {
                    PointFeature feature;
                    // v runs from the tile's north edge.
                    MapPos wgs84 = projection->toWgs84(MapPos(originX + vertex(0) * width, originY - vertex(1) * height));
                    feature.lon = wgs84.getX();
                    feature.lat = wgs84.getY();
                    feature.id = featureId;
                    feature.rank = rank;
                    feature.properties = properties;
                    features.push_back(std::move(feature));
                }
            }
        }

    }

    PointDetailTileDataSource::PointDetailTileDataSource(const std::shared_ptr<TileDataSource>& dataSource, const std::string& layerName, int detailZoom) :
        TileDataSource(),
        _dataSource(dataSource),
        _layerName(layerName),
        _detailZoom(detailZoom),
        _maxDetailLevels(3),
        _maxFeatures(256),
        _rankProperty("ele"),
        _rankPropertyMutex()
    {
        if (!dataSource) {
            throw NullArgumentException("Null dataSource");
        }
        if (layerName.empty()) {
            throw InvalidArgumentException("Empty layerName");
        }

        _dataSourceListener = std::make_shared<DataSourceListener>(*this);
        _dataSource->registerOnChangeListener(_dataSourceListener);
    }

    PointDetailTileDataSource::~PointDetailTileDataSource() {
        _dataSource->unregisterOnChangeListener(_dataSourceListener);
        _dataSourceListener.reset();
    }

    const std::string& PointDetailTileDataSource::getLayerName() const {
        return _layerName;
    }

    int PointDetailTileDataSource::getDetailZoom() const {
        return _detailZoom.load();
    }

    void PointDetailTileDataSource::setDetailZoom(int detailZoom) {
        if (_detailZoom.exchange(detailZoom) != detailZoom) {
            notifyTilesChanged(true);
        }
    }

    int PointDetailTileDataSource::getMaxDetailLevels() const {
        return _maxDetailLevels.load();
    }

    void PointDetailTileDataSource::setMaxDetailLevels(int levels) {
        int clamped = std::max(0, std::min(levels, 8));
        if (_maxDetailLevels.exchange(clamped) != clamped) {
            notifyTilesChanged(true);
        }
    }

    int PointDetailTileDataSource::getMaxFeatures() const {
        return _maxFeatures.load();
    }

    void PointDetailTileDataSource::setMaxFeatures(int maxFeatures) {
        int clamped = std::max(0, maxFeatures);
        if (_maxFeatures.exchange(clamped) != clamped) {
            notifyTilesChanged(true);
        }
    }

    std::string PointDetailTileDataSource::getRankProperty() const {
        std::lock_guard<std::mutex> lock(_rankPropertyMutex);
        return _rankProperty;
    }

    void PointDetailTileDataSource::setRankProperty(const std::string& name) {
        {
            std::lock_guard<std::mutex> lock(_rankPropertyMutex);
            if (_rankProperty == name) {
                return;
            }
            _rankProperty = name;
        }
        notifyTilesChanged(true);
    }

    int PointDetailTileDataSource::getMinZoom() const {
        return _dataSource->getMinZoom();
    }

    int PointDetailTileDataSource::getMaxZoom() const {
        return _dataSource->getMaxZoom();
    }

    MapBounds PointDetailTileDataSource::getDataExtent() const {
        return _dataSource->getDataExtent();
    }

    std::shared_ptr<TileData> PointDetailTileDataSource::loadTile(const MapTile& tile) {
        int detailZoom = _detailZoom.load();
        // At or past the detail zoom the source tile already holds everything.
        if (tile.getZoom() >= detailZoom) {
            return _dataSource->loadTile(tile);
        }

        int levels = std::min(detailZoom - tile.getZoom(), _maxDetailLevels.load());
        if (levels <= 0) {
            return _dataSource->loadTile(tile);
        }

        std::vector<PointFeature> features;
        std::shared_ptr<Projection> projection = getProjection();
        std::string rankProperty = getRankProperty();
        int span = 1 << levels;
        int baseX = tile.getX() * span;
        int baseY = tile.getY() * span;
        for (int dy = 0; dy < span; dy++) {
            for (int dx = 0; dx < span; dx++) {
                try {
                    collectTile(_dataSource, projection, MapTile(baseX + dx, baseY + dy, tile.getZoom() + levels, tile.getFrameNr()), _layerName, rankProperty, features);
                } catch (const std::exception& ex) {
                    // One unreadable descendant is a hole in the rebuilt tile, not a failure of it.
                    Log::Infof("PointDetailTileDataSource::loadTile: Failed to read %d/%d/%d: %s", tile.getZoom() + levels, baseX + dx, baseY + dy, ex.what());
                }
            }
        }
        if (features.empty()) {
            // Pass through: the coarse tile may still carry other layers a style draws.
            return _dataSource->loadTile(tile);
        }

        // Merging undid the source's declustering; without a cap the label culler pays per frame for every point.
        int maxFeaturesSetting = _maxFeatures.load();
        std::size_t maxFeatures = static_cast<std::size_t>(maxFeaturesSetting > 0 ? maxFeaturesSetting : features.size());
        if (features.size() > maxFeatures) {
            std::nth_element(features.begin(), features.begin() + maxFeatures, features.end(), [](const PointFeature& left, const PointFeature& right) {
                return left.rank > right.rank;
            });
            features.resize(maxFeatures);
        }

        try {
            mbvtbuilder::MBVTTileBuilder tileBuilder(tile.getZoom(), tile.getZoom());
            int layerIndex = tileBuilder.createLayer(_layerName);
            for (PointFeature& feature : features) {
                mbvtbuilder::MBVTTileBuilder::MultiPoint coords { mbvtbuilder::MBVTTileBuilder::Point(feature.lon, feature.lat) };
                tileBuilder.addMultiPoint(layerIndex, std::move(coords), picojson::value(static_cast<std::int64_t>(feature.id)), picojson::value(feature.properties), false);
            }

            protobuf::encoded_message encodedTile;
            // The builder's flipped Y is the tile's own Y, so this is the unflipped tile, unlike collectTile.
            tileBuilder.buildTile(tile.getZoom(), tile.getX(), tile.getY(), encodedTile);
            auto data = std::make_shared<BinaryData>(reinterpret_cast<const unsigned char*>(encodedTile.data().data()), encodedTile.data().size());
            auto tileData = std::make_shared<TileData>(data);
            applyTileMetaData(tileData);
            return tileData;
        } catch (const std::exception& ex) {
            Log::Errorf("PointDetailTileDataSource::loadTile: Failed to build %s: %s", tile.toString().c_str(), ex.what());
            return _dataSource->loadTile(tile);
        }
    }

    PointDetailTileDataSource::DataSourceListener::DataSourceListener(PointDetailTileDataSource& dataSource) :
        _dataSource(dataSource)
    {
    }

    void PointDetailTileDataSource::DataSourceListener::onTilesChanged(bool removeTiles) {
        _dataSource.notifyTilesChanged(removeTiles);
    }

}
