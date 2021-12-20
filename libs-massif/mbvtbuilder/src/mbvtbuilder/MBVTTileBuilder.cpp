#include "MBVTTileBuilder.h"
#include "MBVTLayerEncoder.h"
#include "Clipper.h"
#include "Simplifier.h"

#include <functional>
#include <algorithm>
#include <stdexcept>
#include <cmath>

#include "mapnikvt/mbvtpackage/MBVTPackage.pb.h"

namespace carto { namespace mbvtbuilder {
    MBVTTileBuilder::MBVTTileBuilder(int minZoom, int maxZoom) :
        _minZoom(minZoom), _maxZoom(maxZoom)
    {
    }

    bool MBVTTileBuilder::isFastSimplifyMode() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _fastSimplifyMode;
    }

    void MBVTTileBuilder::setFastSimplifyMode(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);
        _fastSimplifyMode = enabled;
        invalidateCache();
    }

    float MBVTTileBuilder::getSimplifyTolerance() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _simplifyTolerance;
    }

    void MBVTTileBuilder::setSimplifyTolerance(float tolerance) {
        std::lock_guard<std::mutex> lock(_mutex);
        _simplifyTolerance = tolerance;
        invalidateCache();
    }

    float MBVTTileBuilder::getDefaultLayerBuffer() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _defaultLayerBuffer;
    }

    void MBVTTileBuilder::setDefaultLayerBuffer(float buffer) {
        std::lock_guard<std::mutex> lock(_mutex);
        _defaultLayerBuffer = buffer;
    }

    std::vector<MBVTTileBuilder::LayerIndex> MBVTTileBuilder::getLayerIndices() const {
        std::vector<LayerIndex> layerIndices;
        for (auto it = _layers.begin(); it != _layers.end(); it++) {
            layerIndices.push_back(it->first);
        }
        return layerIndices;
    }

    MBVTTileBuilder::LayerIndex MBVTTileBuilder::createLayer(const std::string& name) {
        std::lock_guard<std::mutex> lock(_mutex);
        Layer layer;
        layer.name = name;
        layer.buffer = _defaultLayerBuffer / TILE_PIXELS;
        LayerIndex layerIndex = (_layers.empty() ? 0 : _layers.rbegin()->first) + 1;
        _layers.emplace(layerIndex, std::move(layer));
        invalidateCache();
        return layerIndex;
    }

    MBVTTileBuilder::Bounds MBVTTileBuilder::getLayerBounds(LayerIndex layerIndex) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _layers.find(layerIndex);
        if (it == _layers.end()) {
            throw std::runtime_error("Invalid layer index");
        }
        return it->second.bounds;
    }

    void MBVTTileBuilder::clearLayer(LayerIndex layerIndex) {
        std::lock_guard<std::mutex> lock(_mutex);
        _layers[layerIndex].bounds = Bounds::smallest();
        _layers[layerIndex].features.clear();
        invalidateCache();
    }

    void MBVTTileBuilder::deleteLayer(LayerIndex layerIndex) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _layers.find(layerIndex);
        if (it == _layers.end()) {
            throw std::runtime_error("Invalid layer index");
        }
        _layers.erase(it);
        invalidateCache();
    }

    void MBVTTileBuilder::addMultiPoint(LayerIndex layerIndex, MultiPoint coords, picojson::value properties) {
        std::transform(coords.begin(), coords.end(), coords.begin(), wgs84ToWM);
        Bounds bounds = Bounds::make_union(coords.begin(), coords.end());

        std::lock_guard<std::mutex> lock(_mutex);
        Feature feature;
        feature.id = extractFeatureId(layerIndex, properties);
        feature.bounds = bounds;
        feature.geometry = std::move(coords);
        feature.properties = std::move(properties);
        
        _layers[layerIndex].bounds.add(feature.bounds);
        _layers[layerIndex].features.push_back(std::move(feature));
        invalidateCache();
    }

    void MBVTTileBuilder::addMultiLineString(LayerIndex layerIndex, MultiLineString coordsList, picojson::value properties) {
        Bounds bounds = Bounds::smallest();
        for (std::vector<Point>& coords : coordsList) {
            std::transform(coords.begin(), coords.end(), coords.begin(), wgs84ToWM);
            bounds.add(Bounds::make_union(coords.begin(), coords.end()));
        }

        std::lock_guard<std::mutex> lock(_mutex);
        Feature feature;
        feature.id = extractFeatureId(layerIndex, properties);
        feature.bounds = bounds;
        feature.geometry = std::move(coordsList);
        feature.properties = std::move(properties);
        
        _layers[layerIndex].bounds.add(feature.bounds);
        _layers[layerIndex].features.push_back(std::move(feature));
        invalidateCache();
    }

    void MBVTTileBuilder::addMultiPolygon(LayerIndex layerIndex, MultiPolygon ringsList, picojson::value properties) {
        Bounds bounds = Bounds::smallest();
        for (std::vector<std::vector<Point>>& rings : ringsList) {
            for (std::vector<Point>& coords : rings) {
                std::transform(coords.begin(), coords.end(), coords.begin(), wgs84ToWM);
                bounds.add(Bounds::make_union(coords.begin(), coords.end()));
            }
        }

        std::lock_guard<std::mutex> lock(_mutex);
        Feature feature;
        feature.id = extractFeatureId(layerIndex, properties);
        feature.bounds = bounds;
        feature.geometry = std::move(ringsList);
        feature.properties = std::move(properties);
        
        _layers[layerIndex].bounds.add(feature.bounds);
        _layers[layerIndex].features.push_back(std::move(feature));
        invalidateCache();
    }

    void MBVTTileBuilder::importGeoJSON(LayerIndex layerIndex, const picojson::value& geoJSON) {
        std::string type = geoJSON.get("type").get<std::string>();
        if (type == "FeatureCollection") {
            importGeoJSONFeatureCollection(layerIndex, geoJSON);
        } else if (type == "Feature") {
            importGeoJSONFeature(layerIndex, geoJSON);
        } else {
            throw std::runtime_error("Unexpected element type");
        }
    }

    void MBVTTileBuilder::importGeoJSONFeatureCollection(LayerIndex layerIndex, const picojson::value& featureCollectionDef) {
        const picojson::array& featuresDef = featureCollectionDef.get("features").get<picojson::array>();

        for (const picojson::value& featureDef : featuresDef) {
            std::string type = featureDef.get("type").get<std::string>();
            if (type != "Feature") {
                throw std::runtime_error("Unexpected element type");
            }
            
            importGeoJSONFeature(layerIndex, featureDef);
        }
    }

    void MBVTTileBuilder::importGeoJSONFeature(LayerIndex layerIndex, const picojson::value& featureDef) {
        const picojson::value& geometryDef = featureDef.get("geometry");
        if (geometryDef.is<picojson::null>()) {
            return;
        }
        const picojson::value& properties = featureDef.get("properties");

        std::string type = geometryDef.get("type").get<std::string>();
        const picojson::value& coordsDef = geometryDef.get("coordinates");

        if (type == "Point") {
            addMultiPoint(layerIndex, { parseCoordinates(coordsDef) }, properties);
        }
        else if (type == "LineString") {
            addMultiLineString(layerIndex, { parseCoordinatesList(coordsDef) }, properties);
        }
        else if (type == "Polygon") {
            addMultiPolygon(layerIndex, { parseCoordinatesRings(coordsDef) }, properties);
        }
        else if (type == "MultiPoint") {
            std::vector<cglib::vec2<double>> coords;
            for (const picojson::value& subCoordsDef : coordsDef.get<picojson::array>()) {
                coords.push_back(parseCoordinates(subCoordsDef));
            }
            addMultiPoint(layerIndex, std::move(coords), properties);
        }
        else if (type == "MultiLineString") {
            std::vector<std::vector<cglib::vec2<double>>> coordsList;
            for (const picojson::value& subCoordsDef : coordsDef.get<picojson::array>()) {
                coordsList.push_back(parseCoordinatesList(subCoordsDef));
            }
            addMultiLineString(layerIndex, std::move(coordsList), properties);
        }
        else if (type == "MultiPolygon") {
            std::vector<std::vector<std::vector<cglib::vec2<double>>>> ringsList;
            for (const picojson::value& subCoordsDef : coordsDef.get<picojson::array>()) {
                ringsList.push_back(parseCoordinatesRings(subCoordsDef));
            }
            addMultiPolygon(layerIndex, std::move(ringsList), properties);
        }
        else {
            throw std::runtime_error("Invalid geometry type");
        }
    }

    void MBVTTileBuilder::buildTile(int zoom, int tileX, int tileY, protobuf::encoded_message& encodedTile) const {
        static const Bounds mapBounds(Point(-PI * EARTH_RADIUS, -PI * EARTH_RADIUS), Point(PI * EARTH_RADIUS, PI * EARTH_RADIUS));

        std::lock_guard<std::mutex> lock(_mutex);
        const std::map<LayerIndex, Layer>& layers = simplifyAndCacheLayers(zoom);
        for (auto it = layers.begin(); it != layers.end(); it++) {
            const Layer& layer = it->second;
            if (layer.features.empty()) {
                continue;
            }

            double tileSize = (mapBounds.max(0) - mapBounds.min(0)) / (1 << zoom);
            Point tileOrigin(tileX * tileSize + mapBounds.min(0), tileY * tileSize + mapBounds.min(1));
            Bounds tileBounds(tileOrigin - cglib::vec2<double>(layer.buffer, layer.buffer) * tileSize, tileOrigin + cglib::vec2<double>(1 + layer.buffer, 1 + layer.buffer) * tileSize);

            MBVTLayerEncoder layerEncoder(layer.name);
            if (encodeLayer(layer, tileOrigin, tileSize, tileBounds, layerEncoder)) {
                encodedTile.write_tag(vector_tile::Tile::kLayersFieldNumber, protobuf::encoded_message::length_type);
                encodedTile.write_message(layerEncoder.buildLayer());
            }
        }
    }

    void MBVTTileBuilder::buildTiles(std::function<void(int, int, int, const protobuf::encoded_message&)> handler) const {
        static const Bounds mapBounds(Point(-PI * EARTH_RADIUS, -PI * EARTH_RADIUS), Point(PI * EARTH_RADIUS, PI * EARTH_RADIUS));

        std::lock_guard<std::mutex> lock(_mutex);
        for (int zoom = _maxZoom; zoom >= _minZoom; zoom--) {
            const std::map<LayerIndex, Layer>& layers = simplifyAndCacheLayers(zoom);

            Bounds layersBounds = Bounds::smallest(); 
            for (auto it = layers.begin(); it != layers.end(); it++) {
                const Layer& layer = it->second;
                layersBounds.add(layer.bounds.min - cglib::vec2<double>(layer.buffer, layer.buffer));
                layersBounds.add(layer.bounds.max + cglib::vec2<double>(layer.buffer, layer.buffer));
            }

            double tileSize = (mapBounds.max(0) - mapBounds.min(0)) / (1 << zoom);
            double tileCount = (1 << zoom);

            double tileX0 = std::max(0.0, std::floor((layersBounds.min(0) - mapBounds.min(0)) / tileSize));
            double tileY0 = std::max(0.0, std::floor((layersBounds.min(1) - mapBounds.min(1)) / tileSize));
            double tileX1 = std::min(tileCount, std::floor((layersBounds.max(0) - mapBounds.min(0)) / tileSize) + 1);
            double tileY1 = std::min(tileCount, std::floor((layersBounds.max(1) - mapBounds.min(1)) / tileSize) + 1);

            for (int tileY = static_cast<int>(tileY0); tileY < tileY1; tileY++) {
                for (int tileX = static_cast<int>(tileX0); tileX < tileX1; tileX++) {
                    protobuf::encoded_message encodedTile;
                    for (auto it = layers.begin(); it != layers.end(); it++) {
                        const Layer& layer = it->second;
                        if (layer.features.empty()) {
                            continue;
                        }

                        Point tileOrigin(tileX * tileSize + mapBounds.min(0), tileY * tileSize + mapBounds.min(1));
                        Bounds tileBounds(tileOrigin - cglib::vec2<double>(layer.buffer, layer.buffer) * tileSize, tileOrigin + cglib::vec2<double>(1 + layer.buffer, 1 + layer.buffer) * tileSize);

                        MBVTLayerEncoder layerEncoder(layer.name);
                        if (encodeLayer(layer, tileOrigin, tileSize, tileBounds, layerEncoder)) {
                            encodedTile.write_tag(vector_tile::Tile::kLayersFieldNumber, protobuf::encoded_message::length_type);
                            encodedTile.write_message(layerEncoder.buildLayer());
                        }
                    }

                    if (!encodedTile.empty()) {
                        handler(zoom, tileX, tileY, encodedTile);
                    }
                }
            }
        }
    }

    std::uint64_t MBVTTileBuilder::extractFeatureId(LayerIndex layerIndex, const picojson::value& properties) {
        if (properties.is<picojson::object>() && properties.contains("id")) {
            const picojson::value& id = properties.get("id");
            if (id.is<std::int64_t>()) {
                return id.get<std::int64_t>();
            }
            if (id.is<std::string>()) {
                return std::hash<std::string>()(id.get<std::string>());
            }
        }

        auto it = _layers.find(layerIndex);
        std::size_t size = (it != _layers.end() ? it->second.features.size() : 0);
        return static_cast<std::uint64_t>(((layerIndex + 1ULL) << 32) + size);
    }

    const std::map<MBVTTileBuilder::LayerIndex, MBVTTileBuilder::Layer>& MBVTTileBuilder::simplifyAndCacheLayers(int zoom) const {
        auto it = _cachedZoomLayers.lower_bound(zoom);
        int nextZoom = (it != _cachedZoomLayers.end() ? it->first : _maxZoom + 1);
        while (nextZoom > zoom) {
            int currentZoom = (!_fastSimplifyMode ? zoom : nextZoom - 1);
            _cachedZoomLayers[currentZoom] = (!_fastSimplifyMode || nextZoom > _maxZoom ? _layers : _cachedZoomLayers[nextZoom]);
            std::map<LayerIndex, Layer>& layers = _cachedZoomLayers[currentZoom];
            for (auto it = layers.begin(); it != layers.end(); it++) {
                Layer& layer = it->second;
                double tolerance = 2.0 * PI * EARTH_RADIUS / (1 << currentZoom) * _simplifyTolerance / (TILE_PIXELS * TILE_SUBPIXEL_TOLERANCE_DIVIDER);
                simplifyLayer(layer, tolerance);
            }
            nextZoom = currentZoom;
        }
        return _cachedZoomLayers[zoom];
    }

    void MBVTTileBuilder::invalidateCache() const {
        _cachedZoomLayers.clear();
    }

    void MBVTTileBuilder::simplifyLayer(Layer& layer, double tolerance) {
        struct GeometryVisitor {
            explicit GeometryVisitor(double tolerance) : _simplifier(tolerance) { }

            void operator() (MultiPoint& coords) {
            }

            void operator() (MultiLineString& coordsList) {
                for (std::vector<Point>& coords : coordsList) {
                    coords = _simplifier.simplifyLineString(coords);
                }
            }

            void operator() (MultiPolygon& ringsList) {
                for (std::vector<std::vector<Point>>& rings : ringsList) {
                    for (auto it = rings.begin(); it != rings.end(); ) {
                        std::vector<Point> simplifiedRing = _simplifier.simplifyPolygonRing(*it);
                        if (simplifiedRing.size() >= 3) {
                            *it++ = std::move(simplifiedRing);
                        } else {
                            it = rings.erase(it);
                        }
                    }
                }
            }

        private:
            const Simplifier<double> _simplifier;
        };

        for (Feature& feature : layer.features) {
            GeometryVisitor visitor(tolerance);
            std::visit(visitor, feature.geometry);
        }
    }

    bool MBVTTileBuilder::encodeLayer(const Layer& layer, const Point& tileOrigin, double tileSize, const Bounds& tileBounds, MBVTLayerEncoder& layerEncoder) {
        struct GeometryVisitor {
            explicit GeometryVisitor(const Point& tileOrigin, double tileSize, const Bounds& tileBounds, std::uint64_t id, const picojson::value& properties, MBVTLayerEncoder& layerEncoder) : _tileOrigin(tileOrigin), _tileScale(1.0 / tileSize), _clipper(tileBounds), _id(id), _properties(properties), _layerEncoder(layerEncoder) { }

            bool operator() (const MultiPoint& coords) {
                std::vector<MBVTLayerEncoder::Point> tileCoords;
                tileCoords.reserve(coords.size());
                for (const Point& pos : coords) {
                    if (_clipper.testPoint(pos)) {
                        tileCoords.push_back(MBVTLayerEncoder::Point::convert((pos - _tileOrigin) * _tileScale));
                    }
                }
                _layerEncoder.addMultiPoint(_id, tileCoords, _properties);
                return !tileCoords.empty();
            }

            bool operator() (const MultiLineString& coordsList) {
                std::vector<std::vector<MBVTLayerEncoder::Point>> tileCoordsList;
                tileCoordsList.reserve(coordsList.size());
                for (const std::vector<Point>& coords : coordsList) {
                    std::vector<std::vector<Point>> clippedCoordsList = _clipper.clipLineString(coords);
                    for (const std::vector<Point>& clippedCoords : clippedCoordsList) {
                        std::vector<MBVTLayerEncoder::Point> tileCoords;
                        tileCoords.reserve(clippedCoords.size());
                        for (const Point& pos : clippedCoords) {
                            tileCoords.push_back(MBVTLayerEncoder::Point::convert((pos - _tileOrigin) * _tileScale));
                        }
                        tileCoordsList.push_back(std::move(tileCoords));
                    }
                }
                _layerEncoder.addMultiLineString(_id, tileCoordsList, _properties);
                return !tileCoordsList.empty();
            }

            bool operator() (const MultiPolygon& ringsList) {
                std::vector<std::vector<MBVTLayerEncoder::Point>> tileCoordsList;
                for (const std::vector<std::vector<Point>>& rings : ringsList) {
                    for (std::size_t i = 0; i < rings.size(); i++) {
                        std::vector<Point> clippedRing = _clipper.clipPolygonRing(rings[i]);
                        if (clippedRing.size() < 3) {
                            continue;
                        }

                        std::vector<MBVTLayerEncoder::Point> tileCoords;
                        tileCoords.reserve(clippedRing.size());
                        double signedArea = 0;
                        Point prevPos = clippedRing.back();
                        for (const Point& pos : clippedRing) {
                            tileCoords.push_back(MBVTLayerEncoder::Point::convert((pos - _tileOrigin) * _tileScale));
                            signedArea += (prevPos(0) - _tileOrigin(0)) * (pos(1) - _tileOrigin(1)) - (prevPos(1) - _tileOrigin(1)) * (pos(0) - _tileOrigin(0));
                            prevPos = pos;
                        }
                        if ((signedArea < 0) != (i == 0)) {
                            std::reverse(tileCoords.begin(), tileCoords.end());
                        }
                        tileCoordsList.push_back(std::move(tileCoords));
                    }
                }
                _layerEncoder.addMultiPolygon(_id, tileCoordsList, _properties);
                return !tileCoordsList.empty();
            }

        private:
            const Point _tileOrigin;
            const double _tileScale;
            const Clipper<double> _clipper;
            const std::uint64_t _id;
            const picojson::value& _properties;
            MBVTLayerEncoder& _layerEncoder;
        };

        bool featuresAdded = false;
        for (const Feature& feature : layer.features) {
            if (!tileBounds.inside(feature.bounds)) {
                continue;
            }

            GeometryVisitor visitor(tileOrigin, tileSize, tileBounds, feature.id, feature.properties, layerEncoder);
            if (std::visit(visitor, feature.geometry)) {
                featuresAdded = true;
            }
        }

        return featuresAdded;
    }

    std::vector<std::vector<MBVTTileBuilder::Point>> MBVTTileBuilder::parseCoordinatesRings(const picojson::value& coordsDef) {
        const picojson::array& coordsArray = coordsDef.get<picojson::array>();

        std::vector<std::vector<Point>> rings;
        rings.reserve(coordsArray.size());
        for (std::size_t i = 0; i < coordsArray.size(); i++) {
            std::vector<Point> coordsList = parseCoordinatesList(coordsArray[i]);
            rings.push_back(std::move(coordsList));
        }
        return rings;
    }

    std::vector<MBVTTileBuilder::Point> MBVTTileBuilder::parseCoordinatesList(const picojson::value& coordsDef) {
        const picojson::array& coordsArray = coordsDef.get<picojson::array>();

        std::vector<Point> coordsList;
        coordsList.reserve(coordsArray.size());
        for (std::size_t i = 0; i < coordsArray.size(); i++) {
            Point coords = parseCoordinates(coordsArray[i]);
            coordsList.push_back(coords);
        }
        return coordsList;
    }

    MBVTTileBuilder::Point MBVTTileBuilder::parseCoordinates(const picojson::value& coordsDef) {
        const picojson::array& coordsArray = coordsDef.get<picojson::array>();
        return cglib::vec2<double>(coordsArray.at(0).get<double>(), coordsArray.at(1).get<double>());
    }

    MBVTTileBuilder::Point MBVTTileBuilder::wgs84ToWM(const cglib::vec2<double>& posWgs84) {
        double x = EARTH_RADIUS * posWgs84(0) * PI / 180.0;
        double a = posWgs84(1) * PI / 180.0;
        double y = 0.5 * EARTH_RADIUS * std::log((1.0 + std::sin(a)) / (1.0 - std::sin(a)));
        return Point(x, -y); // NOTE: we use EPSG3857 with flipped Y
    }
} }
