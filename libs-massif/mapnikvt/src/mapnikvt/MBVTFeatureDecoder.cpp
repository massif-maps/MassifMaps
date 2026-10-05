#include "MBVTFeatureDecoder.h"
#include "MBVTGeometryBounds.h"
#include "CompressionUtils.h"
#include "Predicate.h"
#include "Logger.h"

#include "mbvtpackage/MBVTPackage.pb.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <atomic>
#include <list>
#include <vector>
#include <map>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <limits>

#include <stdext/zlib.h>

namespace massif::mvt {
    class MBVTFeatureDecoder::MBVTFeatureIterator : public massif::mvt::FeatureDecoder::FeatureIterator {
        friend class MBVTFeatureDecoder;

    public:
        explicit MBVTFeatureIterator(const std::shared_ptr<const vector_tile::Tile>& tile, int layerIndex, const std::set<std::string>* fields, const cglib::mat3x3<float>& transform, const cglib::bbox2<float>& clipBox, bool featureIdOverride, long long tileIdOffset, const std::shared_ptr<MBVTFeatureDecoder::GeometryCache>& geometryCache, const std::shared_ptr<MBVTFeatureDecoder::FeatureDataCache<std::vector<int>>>& featureDataCache, const std::shared_ptr<const std::vector<cglib::bbox2<float>>>& featureBounds = std::shared_ptr<const std::vector<cglib::bbox2<float>>>()) :
            _tile(tile), _layer(&tile->layers(layerIndex)), _transform(transform), _clipBox(clipBox), _featureIdOverride(featureIdOverride), _tileIdOffset(tileIdOffset), _geometryCache(geometryCache), _featureDataCache(featureDataCache), _featureBounds(featureBounds)
        {
            _layerIndexOffset = static_cast<long long>(layerIndex) << 32;

            // Detect if there are any keys we can use as a feature id...
            _keyFieldMap.resize(_layer->keys_size(), -1);
            for (int i = 0; i < _layer->keys_size(); i++) {
                if (_layer->keys(i) == "id" || _layer->keys(i) == "osm_id" || _layer->keys(i) == "cartodb_id") {
                    _idKey = i;
                }
                if (fields) {
                    auto it = fields->find(_layer->keys(i));
                    if (it != fields->end()) {
                        _keyFieldMap[i] = static_cast<int>(_fieldKeys.size());
                        _fieldKeys.push_back(i);
                    }
                }
                else {
                    _keyFieldMap[i] = static_cast<int>(_fieldKeys.size());
                    _fieldKeys.push_back(i);
                }
            }
            skipClipped();
        }

        bool findByLocalId(long long localId) {
            if (localId >= _layerIndexOffset && localId < _layerIndexOffset + _layer->features_size()) {
                _index = static_cast<int>(localId - _layerIndexOffset);
                return true;
            }
            return false;
        }

        virtual bool valid() const override {
            return _index < _layer->features_size();
        }

        virtual void advance() override {
            _index++;
            skipClipped();
        }

        virtual long long getLocalId() const override {
            return _layerIndexOffset + _index;
        }

        virtual long long getFeatureId() const override {
            // If in id override mode, generate id automatically based on feature index
            if (_featureIdOverride) {
                return _tileIdOffset + _index;
            }

            // If feature has a valid id, use it
            const vector_tile::Tile::Feature& feature = _layer->features(_index);
            if (feature.id() != 0) {
                return feature.id();
            }
            else if (_idKey != -1) {
                // Find the id key from feature tags and use it
                for (int i = 0; i + 1 < feature.tags_size(); i += 2) {
                    if (feature.tags(i) == _idKey) {
                        int valueIdx = feature.tags(i + 1);
                        if (valueIdx >= 0 && valueIdx < _layer->values_size()) {
                            const vector_tile::Tile::Value& value = _layer->values(valueIdx);
                            if (value.has_int_value()) {
                                return static_cast<long long>(value.int_value());
                            }
                            else if (value.has_sint_value()) {
                                return static_cast<long long>(value.sint_value());
                            }
                            else if (value.has_uint_value()) {
                                return static_cast<long long>(value.uint_value());
                            }
                        }
                    }
                }
            }

            // Return element index
            return 0;
        }

        virtual std::shared_ptr<const FeatureData> getFeatureData(bool explicitFeatureId, const std::set<std::string>* fields) const override {
            const vector_tile::Tile::Feature& feature = _layer->features(_index);
            if (_fieldKeys.empty() || (fields && fields->empty())) {
                if (!explicitFeatureId) {
                    return emptyFeatureData(feature.type());
                }
            }

            const std::vector<int>& keyFieldMap = fieldKeyFieldMap(fields);
            std::vector<int>& tags = _tags;
            tags.assign(_fieldKeys.size() + 1, -1);
            tags.back() = static_cast<int>(feature.type());
            for (int k = 0; k + 1 < feature.tags_size(); k += 2) {
                int keyIdx = feature.tags(k);
                if (keyIdx >= 0 && keyIdx < _layer->keys_size() && keyFieldMap[keyIdx] >= 0) {
                    tags[keyFieldMap[keyIdx]] = feature.tags(k + 1);
                }
            }

            if (!explicitFeatureId) {
                if (std::shared_ptr<const FeatureData> featureData = _featureDataCache->get(tags)) {
                    return featureData;
                }
            }

            FeatureData::GeometryType geomType = convertGeometryType(feature.type());
            std::vector<std::pair<std::string, Value>> dataMap;
            dataMap.reserve(tags.size());
            for (std::size_t i = 0; i < _fieldKeys.size(); i++) {
                int valueIdx = tags[i];
                if (valueIdx >= 0 && valueIdx < _layer->values_size()) {
                    dataMap.emplace_back(_layer->keys(_fieldKeys[i]), convertValue(_layer->values(valueIdx)));
                }
            }

            auto featureData = std::make_shared<FeatureData>(explicitFeatureId ? getFeatureId() : 0, geomType, std::move(dataMap));
            if (!explicitFeatureId) {
                _featureDataCache->put(tags, featureData);
            }
            return featureData;
        }

        virtual std::shared_ptr<const Geometry> getGeometry() const override {
            if (auto geometry = _geometryCache->get(_index)) {
                return geometry;
            }
            
            std::vector<std::vector<cglib::vec2<float>>> verticesList;
            decodeGeometry(_layer->features(_index), verticesList, 1.0f / _layer->extent());

            cglib::bbox2<float> bbox = cglib::bbox2<float>::smallest();
            for (std::vector<cglib::vec2<float>>& vertices : verticesList) {
                for (cglib::vec2<float>& p : vertices) {
                    p = cglib::transform_point(p, _transform);
                    bbox.add(p);
                }
            }
            if (!bbox.inside(_clipBox)) {
                return std::shared_ptr<Geometry>();
            }
            // Sources merge features across their tile (one multipoint of every housenumber):
            // overzoomed, keep only the parts this tile shows.
            auto partMissesClip = [this](const std::vector<cglib::vec2<float>>& vertices) {
                return _featureBounds && !mbvtPartMeetsClip(vertices, _clipBox);
            };

            switch (_layer->features(_index).type()) {
            case vector_tile::Tile::POINT: {
                    std::vector<int> partIndices = mbvtKeepPointParts(verticesList, partMissesClip);
                    if (!verticesList.empty()) {
                        auto geometry = std::make_shared<Geometry>(PointGeometry(std::move(verticesList), std::move(partIndices)));
                        _geometryCache->put(_index, geometry);
                        return geometry;
                    }
                    return std::shared_ptr<Geometry>();
                }
            case vector_tile::Tile::LINESTRING: {
                    verticesList.erase(std::remove_if(verticesList.begin(), verticesList.end(), partMissesClip), verticesList.end());
                    if (verticesList.empty()) {
                        return std::shared_ptr<Geometry>();
                    }
                    auto geometry = std::make_shared<Geometry>(LineGeometry(std::move(verticesList)));
                    _geometryCache->put(_index, geometry);
                    return geometry;
                }
            case vector_tile::Tile::POLYGON: {
                    PolygonGeometry::PolygonList polygons;
                    if (_layer->has_version() && _layer->version() > 1) {
                        auto it = std::find_if(verticesList.begin(), verticesList.end(), isRingCCW); // find first outer ring
                        while (it != verticesList.end()) {
                            auto it0 = it++;
                            it = std::find_if(it, verticesList.end(), isRingCCW); // find next outer ring
                            polygons.emplace_back(it0, it);
                        }
                    }
                    else {
                        polygons.push_back(std::move(verticesList));
                    }
                    polygons.erase(std::remove_if(polygons.begin(), polygons.end(), [&partMissesClip](const PolygonGeometry::VerticesList& rings) {
                        return rings.empty() || partMissesClip(rings.front());
                    }), polygons.end());
                    if (polygons.empty()) {
                        return std::shared_ptr<Geometry>();
                    }
                    auto geometry = std::make_shared<Geometry>(PolygonGeometry(std::move(polygons)));
                    _geometryCache->put(_index, geometry);
                    return geometry;
                }
            default:
                return std::shared_ptr<Geometry>();
            }
        }

    private:
        static const std::shared_ptr<const FeatureData>& emptyFeatureData(vector_tile::Tile::GeomType geomType) {
            switch (geomType) {
            case vector_tile::Tile::POINT: {
                    static const auto pointFeatureData = std::make_shared<const FeatureData>(0, FeatureData::GeometryType::POINT_GEOMETRY, std::vector<std::pair<std::string, Value>>());
                    return pointFeatureData;
                }
            case vector_tile::Tile::LINESTRING: {
                    static const auto lineFeatureData = std::make_shared<const FeatureData>(0, FeatureData::GeometryType::LINE_GEOMETRY, std::vector<std::pair<std::string, Value>>());
                    return lineFeatureData;
                }
            case vector_tile::Tile::POLYGON: {
                    static const auto polygonFeatureData = std::make_shared<const FeatureData>(0, FeatureData::GeometryType::POLYGON_GEOMETRY, std::vector<std::pair<std::string, Value>>());
                    return polygonFeatureData;
                }
            default: {
                    static const auto nullFeatureData = std::make_shared<const FeatureData>(0, FeatureData::GeometryType::NULL_GEOMETRY, std::vector<std::pair<std::string, Value>>());
                    return nullFeatureData;
                }
            }
        }

        static FeatureData::GeometryType convertGeometryType(vector_tile::Tile::GeomType geomType) {
            switch (geomType) {
            case vector_tile::Tile::POINT:
                return FeatureData::GeometryType::POINT_GEOMETRY;
            case vector_tile::Tile::LINESTRING:
                return FeatureData::GeometryType::LINE_GEOMETRY;
            case vector_tile::Tile::POLYGON:
                return FeatureData::GeometryType::POLYGON_GEOMETRY;
            default:
                return FeatureData::GeometryType::NULL_GEOMETRY;
            }
        }

        static Value convertValue(const vector_tile::Tile::Value& val) {
            if (val.has_bool_value()) {
                return Value(val.bool_value());
            }
            else if (val.has_int_value()) {
                return Value(static_cast<long long>(val.int_value()));
            }
            else if (val.has_sint_value()) {
                return Value(static_cast<long long>(val.sint_value()));
            }
            else if (val.has_uint_value()) {
                return Value(static_cast<long long>(val.uint_value()));
            }
            else if (val.has_float_value()) {
                return Value(static_cast<double>(val.float_value()));
            }
            else if (val.has_double_value()) {
                return Value(val.double_value());
            }
            else if (val.has_string_value()) {
                return Value(val.string_value());
            }
            return Value();
        }

        static void decodeGeometry(const vector_tile::Tile::Feature& feature, std::vector<std::vector<cglib::vec2<float>>>& verticesList, float scale) {
            int cx = 0, cy = 0;
            int cmd = 0, length = 0;
            std::vector<cglib::vec2<float>> vertices;
            vertices.reserve(feature.geometry_size());
            for (int i = 0; i < feature.geometry_size(); ) {
                if (length == 0) {
                    int cmdLength = feature.geometry(i++);
                    length = cmdLength >> 3;
                    cmd = cmdLength & 7;
                    if (length == 0) {
                        continue;
                    }
                }

                length--;
                if ((cmd == 1 || cmd == 2) && i + 2 <= feature.geometry_size()) {
                    if (cmd == 1) {
                        if (!vertices.empty()) {
                            verticesList.emplace_back();
                            std::swap(verticesList.back(), vertices);
                        }
                    }
                    int dx = feature.geometry(i++);
                    int dy = feature.geometry(i++);
                    dx = ((dx >> 1) ^ (-(dx & 1)));
                    dy = ((dy >> 1) ^ (-(dy & 1)));
                    cx += dx;
                    cy += dy;
                    vertices.emplace_back(static_cast<float>(cx) * scale, static_cast<float>(cy) * scale);
                }
                else if (cmd == 7) {
                    if (!vertices.empty()) {
                        if (vertices.front() != vertices.back()) {
                            cglib::vec2<float> p = vertices.front();
                            vertices.emplace_back(p);
                        }
                    }
                }
            }
            if (!vertices.empty()) {
                verticesList.emplace_back();
                std::swap(verticesList.back(), vertices);
            }
        }

        // The features getGeometry would clip away, skipped before any style rule reads their tags.
        void skipClipped() {
            if (_featureBounds) {
                while (_index < _layer->features_size() && !mbvtBoundsMeetClip((*_featureBounds)[_index], _transform, _clipBox)) {
                    _index++;
                }
            }
        }

        // _keyFieldMap narrowed to the requested fields. Keyed by the set's address: processLayer asks
        // with the same two sets for every feature, and they outlive the iterator.
        const std::vector<int>& fieldKeyFieldMap(const std::set<std::string>* fields) const {
            if (!fields) {
                return _keyFieldMap;
            }
            for (const std::pair<const std::set<std::string>*, std::vector<int>>& fieldMap : _fieldKeyFieldMaps) {
                if (fieldMap.first == fields) {
                    return fieldMap.second;
                }
            }
            std::vector<int> keyFieldMap(_keyFieldMap);
            for (int i = 0; i < _layer->keys_size(); i++) {
                if (keyFieldMap[i] >= 0 && fields->find(_layer->keys(i)) == fields->end()) {
                    keyFieldMap[i] = -1;
                }
            }
            _fieldKeyFieldMaps.emplace_back(fields, std::move(keyFieldMap));
            return _fieldKeyFieldMaps.back().second;
        }

        static bool isRingCCW(const std::vector<cglib::vec2<float>>& vertices) {
            double area = 0;
            if (!vertices.empty()) {
                for (std::size_t i = 1; i < vertices.size(); i++) {
                    area += vertices[i - 1](0) * vertices[i](1) - vertices[i](0) * vertices[i - 1](1);
                }
                area += vertices.back()(0) * vertices.front()(1) - vertices.front()(0) * vertices.back()(1);
            }
            return area > 0;
        }

        int _index = 0;
        int _idKey = -1;
        long long _layerIndexOffset = 0;
        std::vector<int> _fieldKeys;
        std::vector<int> _keyFieldMap;
        mutable std::list<std::pair<const std::set<std::string>*, std::vector<int>>> _fieldKeyFieldMaps;
        mutable std::vector<int> _tags;
        std::shared_ptr<const vector_tile::Tile> _tile;
        const vector_tile::Tile::Layer* _layer;
        const cglib::mat3x3<float> _transform;
        const cglib::bbox2<float> _clipBox;
        const bool _featureIdOverride;
        const long long _tileIdOffset;

        mutable std::shared_ptr<MBVTFeatureDecoder::GeometryCache> _geometryCache;
        mutable std::shared_ptr<MBVTFeatureDecoder::FeatureDataCache<std::vector<int>>> _featureDataCache;
        const std::shared_ptr<const std::vector<cglib::bbox2<float>>> _featureBounds;
    };

    MBVTFeatureDecoder::MBVTFeatureDecoder(const std::vector<unsigned char>& data, std::shared_ptr<Logger> logger) :
        _logger(std::move(logger))
    {
        std::vector<unsigned char> uncompressedData;
        if (compression::inflate_tile(data.empty() ? nullptr : data.data(), data.size(), uncompressedData)) {
            protobuf::message tileMsg(uncompressedData.data(), uncompressedData.size());
            _tile = std::make_shared<vector_tile::Tile>(tileMsg);
        } else {
            // As a last fallback, try raw protobuf (uncompressed)
            protobuf::message tileMsg(data.data(), data.size());
            _tile = std::make_shared<vector_tile::Tile>(tileMsg);
        }

        for (int i = 0; i < _tile->layers_size(); i++) {
            const std::string& name = _tile->layers(i).name();
            if (_layerMap.find(name) != _layerMap.end()) {
                _logger->write(Logger::Severity::ERROR, "Duplicate layer name: " + name);
            }
            else {
                _layerMap[name] = i;
            }
        }
    }

    void MBVTFeatureDecoder::invalidateGeometryCache() {
        _layerGeometryCache.first.clear();
        _layerGeometryCache.second.reset();
    }

    std::vector<std::string> MBVTFeatureDecoder::getLayerNames() const {
        std::vector<std::string> layerNames;
        for (int i = 0; i < _tile->layers_size(); i++) {
            layerNames.push_back(_tile->layers(i).name());
        }
        return layerNames;
    }

    bool MBVTFeatureDecoder::hasLayer(const std::string& name) const {
        return _layerMap.find(name) != _layerMap.end();
    }

    std::shared_ptr<FeatureDecoder::FeatureIterator> MBVTFeatureDecoder::createLayerFeatureIterator(const std::string& name, const std::set<std::string>* fields, bool clip) const {
        auto layerIt = _layerMap.find(name);
        if (layerIt == _layerMap.end()) {
            return std::shared_ptr<FeatureIterator>();
        }
        int layerIndex = layerIt->second;

        // Its own caches: the shared geometry cache holds only features the clip box KEPT, and
        // getGeometry returns a cached one without re-testing it, so an unclipped pass filling that
        // cache would hand the drawing pass features from the next tile over.
        if (!clip) {
            auto geometryCache = std::make_shared<GeometryCache>();
            geometryCache->reserve(_tile->layers(layerIndex).features_size());
            auto featureDataCache = std::make_shared<FeatureDataCache<std::vector<int>>>();
            cglib::bbox2<float> everything(cglib::vec2<float>(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()), cglib::vec2<float>(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));
            return std::make_shared<MBVTFeatureIterator>(_tile, layerIndex, fields, _transform, everything, _featureIdOverride, _tileIdOffset, geometryCache, featureDataCache);
        }

        std::lock_guard<std::mutex> lock(_layerCacheMutex);
        std::string key = name;
        if (_layerGeometryCache.first != key) {
            _layerGeometryCache.first = key;
            _layerGeometryCache.second.reset();
        }
        std::shared_ptr<GeometryCache>& geometryCache = _layerGeometryCache.second;
        if (!geometryCache) {
            geometryCache = std::make_shared<GeometryCache>();
            geometryCache->reserve(_tile->layers(layerIndex).features_size());
        }

        if (fields) {
            for (const std::string& field : *fields) {
                key.append(1, 0).append(field);
            }
        }
        // One per field set, kept for the whole tile: the styles of a layer alternate between a few
        // field sets, and a single slot rebuilt every feature's data on each pass
        std::shared_ptr<FeatureDataCache<std::vector<int>>>& featureDataCache = _layerFeatureDataCaches[key];
        if (!featureDataCache) {
            featureDataCache = std::make_shared<FeatureDataCache<std::vector<int>>>();
            featureDataCache->reserve(_tile->layers(layerIndex).features_size());
        }

        // Overzoomed, the clip keeps a sliver of the source; without this every style filters every feature.
        std::shared_ptr<const std::vector<cglib::bbox2<float>>> featureBounds;
        if (_transform(0, 0) > 1.0f) {
            std::shared_ptr<const std::vector<cglib::bbox2<float>>>& layerBounds = _layerFeatureBounds[layerIndex];
            if (!layerBounds) {
                const vector_tile::Tile::Layer& layer = _tile->layers(layerIndex);
                auto bounds = std::make_shared<std::vector<cglib::bbox2<float>>>();
                bounds->reserve(layer.features_size());
                for (int i = 0; i < layer.features_size(); i++) {
                    const vector_tile::Tile::Feature& feature = layer.features(i);
                    bounds->push_back(mbvtGeometryBounds(feature.geometry_size(), [&feature](int j) { return static_cast<int>(feature.geometry(j)); }, 1.0f / layer.extent()));
                }
                layerBounds = bounds;
            }
            featureBounds = layerBounds;
        }
        return std::make_shared<MBVTFeatureIterator>(_tile, layerIndex, fields, _transform, _clipBox, _featureIdOverride, _tileIdOffset, geometryCache, featureDataCache, featureBounds);
    }

    bool MBVTFeatureDecoder::mayHaveFieldValue(const std::string& layerName, const std::string& field, const Value& value) const {
        auto layerIt = _layerMap.find(layerName);
        if (layerIt == _layerMap.end()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(_layerCacheMutex);
        auto valuesIt = _layerFieldValues.find(std::make_pair(layerIt->second, field));
        if (valuesIt == _layerFieldValues.end()) {
            const vector_tile::Tile::Layer& layer = _tile->layers(layerIt->second);
            std::vector<bool> keyMatches(layer.keys_size(), false);
            for (int i = 0; i < layer.keys_size(); i++) {
                keyMatches[i] = layer.keys(i) == field;
            }
            std::vector<bool> valueSeen(layer.values_size(), false);
            for (int i = 0; i < layer.features_size(); i++) {
                const vector_tile::Tile::Feature& feature = layer.features(i);
                for (int k = 0; k + 1 < feature.tags_size(); k += 2) {
                    int keyIdx = feature.tags(k), valueIdx = feature.tags(k + 1);
                    if (keyIdx >= 0 && keyIdx < layer.keys_size() && keyMatches[keyIdx] && valueIdx >= 0 && valueIdx < layer.values_size()) {
                        valueSeen[valueIdx] = true;
                    }
                }
            }
            std::vector<Value> values;
            for (int i = 0; i < layer.values_size(); i++) {
                if (valueSeen[i]) {
                    values.push_back(MBVTFeatureIterator::convertValue(layer.values(i)));
                }
            }
            valuesIt = _layerFieldValues.emplace(std::make_pair(layerIt->second, field), std::move(values)).first;
        }
        for (const Value& fieldValue : valuesIt->second) {
            if (ComparisonPredicate::applyOp(ComparisonPredicate::Op::EQ, fieldValue, value)) {
                return true;
            }
        }
        return false;
    }

    bool MBVTFeatureDecoder::findFeature(long long localId, std::string& layerName, Feature& feature) const {
        for (int i = 0; i < _tile->layers_size(); i++) {
            auto geometryCache = std::make_shared<GeometryCache>();
            auto featureDataCache = std::make_shared<FeatureDataCache<std::vector<int>>>();
            MBVTFeatureIterator it(_tile, i, nullptr, _transform, _clipBox, _featureIdOverride, _tileIdOffset, geometryCache, featureDataCache);
            if (it.findByLocalId(localId)) {
                layerName = _tile->layers(i).name();
                feature = Feature(it.getFeatureId(), it.getGeometry(), it.getFeatureData(true, nullptr));
                return true;
            }
        }
        return false;
    }
}
