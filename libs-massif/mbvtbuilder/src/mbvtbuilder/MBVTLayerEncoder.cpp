#include "MBVTLayerEncoder.h"

#include <algorithm>
#include <numeric>

#include "mapnikvt/mbvtpackage/MBVTPackage.pb.h"

namespace carto { namespace mbvtbuilder {
    MBVTLayerEncoder::MBVTLayerEncoder(const std::string& name, int version, int extent) :
        _name(name),
        _version(version > 0 ? version : DEFAULT_LAYER_VERSION),
        _extent(extent > 0 ? extent : DEFAULT_LAYER_EXTENT)
    {
    }

    void MBVTLayerEncoder::addMultiPoint(std::uint64_t id, const std::vector<Point>& coords, const picojson::value& properties) {
        std::vector<std::uint32_t> geometry = encodePointCoordinates(coords, static_cast<float>(_extent));
        importEncodedFeature(id, static_cast<int>(vector_tile::Tile_GeomType_POINT), geometry, properties);
    }
    
    void MBVTLayerEncoder::addMultiLineString(std::uint64_t id, const std::vector<std::vector<Point>>& coordsList, const picojson::value& properties) {
        std::vector<std::uint32_t> geometry = encodeLineStringCoordinates(coordsList, static_cast<float>(_extent));
        importEncodedFeature(id, static_cast<int>(vector_tile::Tile_GeomType_LINESTRING), geometry, properties);
    }
    
    void MBVTLayerEncoder::addMultiPolygon(std::uint64_t id, const std::vector<std::vector<Point>>& ringsList, const picojson::value& properties) {
        std::vector<std::uint32_t> geometry = encodePolygonRingCoordinates(ringsList, static_cast<float>(_extent));
        importEncodedFeature(id, static_cast<int>(vector_tile::Tile_GeomType_POLYGON), geometry, properties);
    }
    
    protobuf::encoded_message MBVTLayerEncoder::buildLayer() const {
        protobuf::encoded_message encodedLayer;

        encodedLayer.write_tag(vector_tile::Tile_Layer::kVersionFieldNumber);
        encodedLayer.write_uint32(_version);

        encodedLayer.write_tag(vector_tile::Tile_Layer::kExtentFieldNumber);
        encodedLayer.write_uint32(_extent);

        encodedLayer.write_tag(vector_tile::Tile_Layer::kNameFieldNumber);
        encodedLayer.write_string(_name);

        for (const std::string& key : _keys) {
            encodedLayer.write_tag(vector_tile::Tile_Layer::kKeysFieldNumber);
            encodedLayer.write_string(key);
        }

        for (const picojson::value& value : _values) {
            encodedLayer.write_tag(vector_tile::Tile_Layer::kValuesFieldNumber);
            encodedLayer.write_message(encodeValue(value));
        }

        for (const protobuf::encoded_message& encodedFeature : _encodedFeatures) {
            encodedLayer.write_tag(vector_tile::Tile_Layer::kFeaturesFieldNumber);
            encodedLayer.write_message(encodedFeature);
        }

        return encodedLayer;
    }

    void MBVTLayerEncoder::importEncodedFeature(std::uint64_t id, int type, const std::vector<std::uint32_t>& geometry, const picojson::value& properties) {
        if (geometry.empty()) {
            return;
        }

        std::vector<std::uint32_t> tags;
        if (properties.is<picojson::object>()) {
            tags.reserve(properties.get<picojson::object>().size() * 2);
            for (std::pair<std::string, picojson::value> keyValuePair : properties.get<picojson::object>()) {
                importProperty(keyValuePair.first, keyValuePair.second, tags);
            }
        }

        protobuf::encoded_message encodedFeature = encodeFeature(id, type, tags, geometry);
        _encodedFeatures.push_back(std::move(encodedFeature));
    }

    void MBVTLayerEncoder::importProperty(const std::string& key, const picojson::value& value, std::vector<std::uint32_t>& tags) {
        if (value.is<picojson::array>()) {
            const picojson::array& valueArr = value.get<picojson::array>();
            for (std::size_t i = 0; i < valueArr.size(); i++) {
                importProperty(key + "[" + picojson::value(static_cast<std::int64_t>(i)).serialize() + "]", valueArr[i], tags);
            }
            return;
        } else if (value.is<picojson::object>()) {
            const picojson::object& valueObj = value.get<picojson::object>();
            for (std::pair<std::string, picojson::value> keyValuePair : valueObj) {
                importProperty(key + "." + keyValuePair.first, keyValuePair.second, tags);
            }
            return;
        }

        auto it1 = _keyIndexMap.find(key);
        if (it1 == _keyIndexMap.end()) {
            it1 = _keyIndexMap.emplace(key, _keys.size()).first;
            _keys.push_back(key);
        }
        tags.push_back(static_cast<std::uint32_t>(it1->second));

        std::string serializedValue = value.serialize();
        auto it2 = _valueIndexMap.find(serializedValue);
        if (it2 == _valueIndexMap.end()) {
            it2 = _valueIndexMap.emplace(serializedValue, _values.size()).first;
            _values.push_back(value);
        }
        tags.push_back(static_cast<std::uint32_t>(it2->second));
    }

    std::vector<std::uint32_t> MBVTLayerEncoder::encodePointCoordinates(const std::vector<Point>& coords, float scale) {
        std::vector<std::uint32_t> encodedCoords;
        std::size_t vertexCount = coords.size();
        encodedCoords.reserve(vertexCount * 3);
        cglib::vec2<std::int32_t> prevIntCoords(0, 0);
        for (std::size_t i = 0; i < coords.size(); i++) {
            cglib::vec2<std::int32_t> intCoords = cglib::vec2<std::int32_t>::convert(coords[i] * scale);
            encodedCoords.push_back(1 | (1 << 3));
            pushZigZagCoords(encodedCoords, intCoords, prevIntCoords);
            prevIntCoords = intCoords;
        }
        return encodedCoords;
    }

    std::vector<std::uint32_t> MBVTLayerEncoder::encodeLineStringCoordinates(const std::vector<std::vector<Point>>& coordsList, float scale) {
        std::vector<std::uint32_t> encodedCoords;
        std::size_t vertexCount = std::accumulate(coordsList.begin(), coordsList.end(), std::size_t(0), [](std::size_t count, const std::vector<Point>& coords) { return count + coords.size(); });
        encodedCoords.reserve(vertexCount * 3);
        cglib::vec2<std::int32_t> prevIntCoords(0, 0);
        for (const std::vector<Point>& coords : coordsList) {
            for (std::size_t i = 0; i < coords.size(); i++) {
                cglib::vec2<std::int32_t> intCoords = cglib::vec2<std::int32_t>::convert(coords[i] * scale);
                if (i > 1 && intCoords == prevIntCoords) {
                    continue;
                }
                encodedCoords.push_back((i == 0 ? 1 : 2) | (1 << 3));
                pushZigZagCoords(encodedCoords, intCoords, prevIntCoords);
                prevIntCoords = intCoords;
            }
        }
        return encodedCoords;
    }

    std::vector<std::uint32_t> MBVTLayerEncoder::encodePolygonRingCoordinates(const std::vector<std::vector<Point>>& ringsList, float scale) {
        std::vector<std::uint32_t> encodedCoords;
        std::size_t vertexCount = std::accumulate(ringsList.begin(), ringsList.end(), std::size_t(0), [](std::size_t count, const std::vector<Point>& coords) { return count + coords.size(); });
        encodedCoords.reserve(vertexCount * 3 + ringsList.size());
        cglib::vec2<std::int32_t> prevIntCoords(0, 0);
        for (std::size_t n = 0; n < ringsList.size(); n++) {
            std::vector<Point> coords = ringsList[n];

            double area = 0;
            if (!coords.empty()) {
                for (std::size_t i = 1; i < coords.size(); i++) {
                    area += coords[i - 1](0) * coords[i](1) - coords[i](0) * coords[i - 1](1);
                }
                area += coords.back()(0) * coords.front()(1) - coords.front()(0) * coords.back()(1);
            }
            if ((n > 0) != (area < 0)) {
                std::reverse(coords.begin(), coords.end());
            }

            for (std::size_t i = 0; i < coords.size(); i++) {
                cglib::vec2<std::int32_t> intCoords = cglib::vec2<std::int32_t>::convert(coords[i] * scale);
                if (i > 1 && intCoords == prevIntCoords) {
                    continue;
                }
                encodedCoords.push_back((i == 0 ? 1 : 2) | (1 << 3));
                pushZigZagCoords(encodedCoords, intCoords, prevIntCoords);
                prevIntCoords = intCoords;
            }
            encodedCoords.push_back(7 | (1 << 3));
        }
        return encodedCoords;
    }

    protobuf::encoded_message MBVTLayerEncoder::encodeValue(const picojson::value& val) {
        protobuf::encoded_message encodedValue;
        if (val.is<std::int64_t>()) {
            std::int64_t intVal = val.get<std::int64_t>();
            if (intVal >= 0) {
                encodedValue.write_tag(vector_tile::Tile_Value::kUintValueFieldNumber);
                encodedValue.write_uint64(intVal);
            } else {
                encodedValue.write_tag(vector_tile::Tile_Value::kSintValueFieldNumber);
                encodedValue.write_sint64(intVal);
            }
        }
        else if (val.is<double>()) {
            double doubleVal = val.get<double>();
            float floatVal = static_cast<float>(doubleVal);
            if (floatVal == doubleVal) {
                encodedValue.write_tag(vector_tile::Tile_Value::kFloatValueFieldNumber);
                encodedValue.write_float(floatVal);
            } else {
                encodedValue.write_tag(vector_tile::Tile_Value::kDoubleValueFieldNumber);
                encodedValue.write_double(doubleVal);
            }
        }
        else if (val.is<bool>()) {
            encodedValue.write_tag(vector_tile::Tile_Value::kBoolValueFieldNumber);
            encodedValue.write_bool(val.get<bool>());
        }
        else if (val.is<std::string>()) {
            encodedValue.write_tag(vector_tile::Tile_Value::kStringValueFieldNumber);
            encodedValue.write_string(val.get<std::string>());
        }
        else {
            // Null
        }
        return encodedValue;
    }

    protobuf::encoded_message MBVTLayerEncoder::encodeFeature(std::uint64_t id, int type, const std::vector<uint32_t>& tags, const std::vector<std::uint32_t>& geometry) {
        protobuf::encoded_message encodedFeature;
        
        encodedFeature.write_tag(vector_tile::Tile_Feature::kTypeFieldNumber);
        encodedFeature.write_uint32(type);
        
        if (id) {
            encodedFeature.write_tag(vector_tile::Tile_Feature::kIdFieldNumber);
            encodedFeature.write_uint64(id);
        }
        
        protobuf::encoded_message encodedTags;
        for (std::uint32_t tag : tags) {
            encodedTags.write_uint32(tag);
        }
        if (!encodedTags.empty()) {
            encodedFeature.write_tag(vector_tile::Tile_Feature::kTagsFieldNumber);
            encodedFeature.write_message(encodedTags);
        }

        protobuf::encoded_message encodedGeometry;
        for (std::uint32_t geom : geometry) {
            encodedGeometry.write_uint32(geom);
        }
        encodedFeature.write_tag(vector_tile::Tile_Feature::kGeometryFieldNumber);
        encodedFeature.write_message(encodedGeometry);

        return encodedFeature;
    }

    void MBVTLayerEncoder::pushZigZagCoords(std::vector<std::uint32_t>& encodedCoords, const cglib::vec2<int>& intCoords, const cglib::vec2<int>& prevIntCoords) {
        int dx = intCoords(0) - prevIntCoords(0);
        int dy = intCoords(1) - prevIntCoords(1);
        encodedCoords.push_back((dx << 1) ^ (dx >> 31));
        encodedCoords.push_back((dy << 1) ^ (dy >> 31));
    }
} }
