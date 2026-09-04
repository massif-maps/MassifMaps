#include "MLTFeatureDecoder.h"
#include "CompressionUtils.h"
#include "Logger.h"

#include <mlt/decoder.hpp>
#include <mlt/geometry.hpp>
#include <mlt/layer.hpp>
#include <mlt/metadata/tileset.hpp>
#include <mlt/properties.hpp>
#include <mlt/tile.hpp>

#include <algorithm>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace massif::mvt {
    namespace {
        using MLTGeometryType = mlt::metadata::tileset::GeometryType;

        FeatureData::GeometryType convertGeometryType(MLTGeometryType geomType) {
            switch (geomType) {
            case MLTGeometryType::POINT:
            case MLTGeometryType::MULTIPOINT:
                return FeatureData::GeometryType::POINT_GEOMETRY;
            case MLTGeometryType::LINESTRING:
            case MLTGeometryType::MULTILINESTRING:
                return FeatureData::GeometryType::LINE_GEOMETRY;
            case MLTGeometryType::POLYGON:
            case MLTGeometryType::MULTIPOLYGON:
                return FeatureData::GeometryType::POLYGON_GEOMETRY;
            default:
                return FeatureData::GeometryType::NULL_GEOMETRY;
            }
        }

        Value convertProperty(const mlt::Property& property) {
            return std::visit([](const auto& val) -> Value {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    return Value();
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    return Value(val);
                }
                else if constexpr (std::is_same_v<T, std::string_view>) {
                    return Value(std::string(val));
                }
                else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                    return Value(static_cast<double>(val));
                }
                else if constexpr (std::is_integral_v<T>) {
                    return Value(static_cast<long long>(val));
                }
                else {
                    // The remaining alternatives are std::optional wrappers of the above
                    if (!val) {
                        return Value();
                    }
                    using U = typename T::value_type;
                    if constexpr (std::is_same_v<U, bool>) {
                        return Value(*val);
                    }
                    else if constexpr (std::is_same_v<U, float> || std::is_same_v<U, double>) {
                        return Value(static_cast<double>(*val));
                    }
                    else {
                        return Value(static_cast<long long>(*val));
                    }
                }
            }, property);
        }
    }

    class MLTFeatureDecoder::MLTFeatureIterator : public massif::mvt::FeatureDecoder::FeatureIterator {
    public:
        explicit MLTFeatureIterator(const std::shared_ptr<const mlt::MapLibreTile>& tile, const mlt::Layer* layer, int layerIndex, const std::vector<std::string>& layerKeys, const std::set<std::string>* fields, const cglib::mat3x3<float>& transform, const cglib::bbox2<float>& clipBox, bool featureIdOverride, long long tileIdOffset, const std::shared_ptr<MLTFeatureDecoder::GeometryCache>& geometryCache) :
            _tile(tile), _layer(layer), _transform(transform), _clipBox(clipBox), _featureIdOverride(featureIdOverride), _tileIdOffset(tileIdOffset), _geometryCache(geometryCache)
        {
            _layerIndexOffset = static_cast<long long>(layerIndex) << 32;
            _scale = layer->getExtent() > 0 ? 1.0f / layer->getExtent() : 0.0f;

            for (const std::string& key : layerKeys) {
                if (!fields || fields->find(key) != fields->end()) {
                    _fieldKeys.push_back(key);
                }
                if (key == "id" || key == "osm_id" || key == "cartodb_id") {
                    _idKey = key;
                }
            }
        }

        bool findByLocalId(long long localId) {
            if (localId >= _layerIndexOffset && localId < _layerIndexOffset + static_cast<long long>(_layer->getFeatures().size())) {
                _index = static_cast<std::size_t>(localId - _layerIndexOffset);
                return true;
            }
            return false;
        }

        virtual bool valid() const override {
            return _index < _layer->getFeatures().size();
        }

        virtual void advance() override {
            _index++;
        }

        virtual long long getLocalId() const override {
            return _layerIndexOffset + static_cast<long long>(_index);
        }

        virtual long long getFeatureId() const override {
            // If in id override mode, generate id automatically based on feature index
            if (_featureIdOverride) {
                return _tileIdOffset + static_cast<long long>(_index);
            }

            const mlt::Feature& feature = _layer->getFeatures()[_index];
            if (auto id = feature.getID()) {
                return static_cast<long long>(*id);
            }
            if (!_idKey.empty()) {
                if (auto property = feature.getProperty(_idKey, *_layer)) {
                    Value value = convertProperty(*property);
                    if (auto id = std::get_if<long long>(&value)) {
                        return *id;
                    }
                }
            }
            return 0;
        }

        virtual std::shared_ptr<const FeatureData> getFeatureData(bool explicitFeatureId, const std::set<std::string>* fields) const override {
            const mlt::Feature& feature = _layer->getFeatures()[_index];
            FeatureData::GeometryType geomType = convertGeometryType(feature.getGeometry().type);

            std::vector<std::pair<std::string, Value>> dataMap;
            dataMap.reserve(_fieldKeys.size());
            for (const std::string& key : _fieldKeys) {
                if (fields && fields->find(key) == fields->end()) {
                    continue;
                }
                if (auto property = feature.getProperty(key, *_layer)) {
                    dataMap.emplace_back(key, convertProperty(*property));
                }
            }

            return std::make_shared<FeatureData>(explicitFeatureId ? getFeatureId() : 0, geomType, std::move(dataMap));
        }

        virtual std::shared_ptr<const Geometry> getGeometry() const override {
            if (auto geometry = _geometryCache->get(_index)) {
                return geometry;
            }

            const mlt::geometry::Geometry& mltGeometry = _layer->getFeatures()[_index].getGeometry();
            std::vector<std::vector<cglib::vec2<float>>> verticesList;
            std::vector<std::size_t> polygonSizes; // ring counts, one entry per polygon
            switch (mltGeometry.type) {
            case MLTGeometryType::POINT:
                verticesList.push_back(convertCoords({ static_cast<const mlt::geometry::Point&>(mltGeometry).getCoordinate() }));
                break;
            case MLTGeometryType::MULTIPOINT:
                // Kept one vertex per list, the shape MVT features of this type decode to
                for (const mlt::Coordinate& coord : static_cast<const mlt::geometry::MultiPoint&>(mltGeometry).getCoordinates()) {
                    verticesList.push_back(convertCoords({ coord }));
                }
                break;
            case MLTGeometryType::LINESTRING:
                verticesList.push_back(convertCoords(static_cast<const mlt::geometry::LineString&>(mltGeometry).getCoordinates()));
                break;
            case MLTGeometryType::MULTILINESTRING:
                for (const mlt::CoordVec& coords : static_cast<const mlt::geometry::MultiLineString&>(mltGeometry).getLineStrings()) {
                    verticesList.push_back(convertCoords(coords));
                }
                break;
            case MLTGeometryType::POLYGON: {
                    const auto& rings = static_cast<const mlt::geometry::Polygon&>(mltGeometry).getRings();
                    for (const mlt::CoordVec& ring : rings) {
                        verticesList.push_back(convertCoords(ring));
                    }
                    polygonSizes.push_back(rings.size());
                }
                break;
            case MLTGeometryType::MULTIPOLYGON:
                for (const auto& rings : static_cast<const mlt::geometry::MultiPolygon&>(mltGeometry).getPolygons()) {
                    for (const mlt::CoordVec& ring : rings) {
                        verticesList.push_back(convertCoords(ring));
                    }
                    polygonSizes.push_back(rings.size());
                }
                break;
            default:
                return std::shared_ptr<Geometry>();
            }

            cglib::bbox2<float> bbox = cglib::bbox2<float>::smallest();
            for (const std::vector<cglib::vec2<float>>& vertices : verticesList) {
                for (const cglib::vec2<float>& p : vertices) {
                    bbox.add(p);
                }
            }
            if (!bbox.inside(_clipBox)) {
                return std::shared_ptr<Geometry>();
            }

            std::shared_ptr<Geometry> geometry;
            switch (convertGeometryType(mltGeometry.type)) {
            case FeatureData::GeometryType::POINT_GEOMETRY:
                if (verticesList.empty()) {
                    return std::shared_ptr<Geometry>();
                }
                geometry = std::make_shared<Geometry>(PointGeometry(std::move(verticesList)));
                break;
            case FeatureData::GeometryType::LINE_GEOMETRY:
                geometry = std::make_shared<Geometry>(LineGeometry(std::move(verticesList)));
                break;
            case FeatureData::GeometryType::POLYGON_GEOMETRY: {
                    PolygonGeometry::PolygonList polygons;
                    polygons.reserve(polygonSizes.size());
                    auto it = verticesList.begin();
                    for (std::size_t ringCount : polygonSizes) {
                        auto it0 = it;
                        std::advance(it, ringCount);
                        polygons.emplace_back(std::make_move_iterator(it0), std::make_move_iterator(it));
                    }
                    geometry = std::make_shared<Geometry>(PolygonGeometry(std::move(polygons)));
                }
                break;
            default:
                return std::shared_ptr<Geometry>();
            }
            _geometryCache->put(_index, geometry);
            return geometry;
        }

    private:
        std::vector<cglib::vec2<float>> convertCoords(const mlt::CoordVec& coords) const {
            std::vector<cglib::vec2<float>> vertices;
            vertices.reserve(coords.size());
            for (const mlt::Coordinate& coord : coords) {
                vertices.push_back(cglib::transform_point(cglib::vec2<float>(coord.x * _scale, coord.y * _scale), _transform));
            }
            return vertices;
        }

        std::size_t _index = 0;
        std::string _idKey;
        long long _layerIndexOffset = 0;
        float _scale = 0.0f;
        std::vector<std::string> _fieldKeys;
        std::shared_ptr<const mlt::MapLibreTile> _tile;
        const mlt::Layer* _layer;
        const cglib::mat3x3<float> _transform;
        const cglib::bbox2<float> _clipBox;
        const bool _featureIdOverride;
        const long long _tileIdOffset;

        mutable std::shared_ptr<MLTFeatureDecoder::GeometryCache> _geometryCache;
    };

    MLTFeatureDecoder::MLTFeatureDecoder(const std::vector<unsigned char>& data, std::shared_ptr<Logger> logger) :
        _logger(std::move(logger))
    {
        std::vector<unsigned char> uncompressedData;
        const std::vector<unsigned char>& tileData = compression::inflate_tile(data.empty() ? nullptr : data.data(), data.size(), uncompressedData) ? uncompressedData : data;

        mlt::Decoder decoder(true);
        _tile = std::make_shared<const mlt::MapLibreTile>(decoder.decode(mlt::DataView(reinterpret_cast<const char*>(tileData.data()), tileData.size())));

        const std::vector<mlt::Layer>& layers = _tile->getLayers();
        _layerKeys.resize(layers.size());
        for (std::size_t i = 0; i < layers.size(); i++) {
            const std::string& name = layers[i].getName();
            if (_layerMap.find(name) != _layerMap.end()) {
                _logger->write(Logger::Severity::ERROR, "Duplicate layer name: " + name);
            }
            else {
                _layerMap[name] = static_cast<int>(i);
            }

            // The property map is unordered - sort so features of a layer always list them the same way
            for (const auto& property : layers[i].getProperties()) {
                _layerKeys[i].push_back(property.first);
            }
            std::sort(_layerKeys[i].begin(), _layerKeys[i].end());
        }
    }

    MLTFeatureDecoder::~MLTFeatureDecoder() = default;

    bool MLTFeatureDecoder::isTileData(const unsigned char* data, std::size_t size) {
        // An MLT tile is a sequence of (varint layer length, varint layer tag, body) that has to
        // tile the buffer exactly. MVT is protobuf and does not fit that shape.
        std::size_t i = 0;
        int layerCount = 0;
        while (i < size) {
            std::uint64_t length = 0;
            int shift = 0;
            while (true) {
                if (i >= size || shift > 63) {
                    return false;
                }
                unsigned char byte = data[i++];
                length |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
                shift += 7;
                if (!(byte & 0x80)) {
                    break;
                }
            }
            if (length == 0 || length > size - i) {
                return false;
            }
            if (data[i] != 1 && data[i] != 2) { // the layer tags the format defines so far
                return false;
            }
            i += static_cast<std::size_t>(length);
            layerCount++;
        }
        return layerCount > 0;
    }

    void MLTFeatureDecoder::invalidateGeometryCache() {
        _layerGeometryCache.first.clear();
        _layerGeometryCache.second.reset();
    }

    std::vector<std::string> MLTFeatureDecoder::getLayerNames() const {
        std::vector<std::string> layerNames;
        layerNames.reserve(_tile->getLayers().size());
        for (const mlt::Layer& layer : _tile->getLayers()) {
            layerNames.push_back(layer.getName());
        }
        return layerNames;
    }

    bool MLTFeatureDecoder::hasLayer(const std::string& name) const {
        return _layerMap.find(name) != _layerMap.end();
    }

    std::shared_ptr<FeatureDecoder::FeatureIterator> MLTFeatureDecoder::createLayerFeatureIterator(const std::string& name, const std::set<std::string>* fields, bool clip) const {
        auto layerIt = _layerMap.find(name);
        if (layerIt == _layerMap.end()) {
            return std::shared_ptr<FeatureIterator>();
        }
        int layerIndex = layerIt->second;
        const mlt::Layer& layer = _tile->getLayers()[layerIndex];

        // Its own cache - see MBVTFeatureDecoder for why an unclipped pass must not fill the
        // shared one.
        if (!clip) {
            auto geometryCache = std::make_shared<GeometryCache>();
            geometryCache->reserve(layer.getFeatures().size());
            cglib::bbox2<float> everything(cglib::vec2<float>(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()), cglib::vec2<float>(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));
            return std::make_shared<MLTFeatureIterator>(_tile, &layer, layerIndex, _layerKeys[layerIndex], fields, _transform, everything, _featureIdOverride, _tileIdOffset, geometryCache);
        }

        std::lock_guard<std::mutex> lock(_layerCacheMutex);
        if (_layerGeometryCache.first != name) {
            _layerGeometryCache.first = name;
            _layerGeometryCache.second.reset();
        }
        std::shared_ptr<GeometryCache>& geometryCache = _layerGeometryCache.second;
        if (!geometryCache) {
            geometryCache = std::make_shared<GeometryCache>();
            geometryCache->reserve(layer.getFeatures().size());
        }

        return std::make_shared<MLTFeatureIterator>(_tile, &layer, layerIndex, _layerKeys[layerIndex], fields, _transform, _clipBox, _featureIdOverride, _tileIdOffset, geometryCache);
    }

    bool MLTFeatureDecoder::findFeature(long long localId, std::string& layerName, Feature& feature) const {
        const std::vector<mlt::Layer>& layers = _tile->getLayers();
        for (std::size_t i = 0; i < layers.size(); i++) {
            auto geometryCache = std::make_shared<GeometryCache>();
            MLTFeatureIterator it(_tile, &layers[i], static_cast<int>(i), _layerKeys[i], nullptr, _transform, _clipBox, _featureIdOverride, _tileIdOffset, geometryCache);
            if (it.findByLocalId(localId)) {
                layerName = layers[i].getName();
                feature = Feature(it.getFeatureId(), it.getGeometry(), it.getFeatureData(true, nullptr));
                return true;
            }
        }
        return false;
    }
}
