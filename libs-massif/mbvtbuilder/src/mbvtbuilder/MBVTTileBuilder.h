/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MBVTBUILDER_MBVTTILEBUILDER_H_
#define _CARTO_MBVTBUILDER_MBVTTILEBUILDER_H_

#include <cstdint>
#include <variant>
#include <vector>
#include <mutex>

#include <boost/math/constants/constants.hpp>

#include <cglib/vec.h>
#include <cglib/bbox.h>

#include <picojson/picojson.h>

#include <protobuf/encodedpbf.hpp>

namespace carto::mbvtbuilder {
    class MBVTLayerEncoder;
    
    class MBVTTileBuilder final {
    public:
        using LayerIndex = int;

        using Bounds = cglib::bbox2<double>;
        using Point = cglib::vec2<double>;
        using MultiPoint = std::vector<Point>;
        using MultiLineString = std::vector<std::vector<Point>>;
        using MultiPolygon = std::vector<std::vector<std::vector<Point>>>;
        
        explicit MBVTTileBuilder(int minZoom, int maxZoom);

        bool isFastSimplifyMode() const;
        void setFastSimplifyMode(bool enabled);

        float getSimplifyTolerance() const;
        void setSimplifyTolerance(float tolerance);

        float getDefaultLayerBuffer() const;
        void setDefaultLayerBuffer(float buffer);

        std::vector<LayerIndex> getLayerIndices() const;
        LayerIndex createLayer(const std::string& name);
        Bounds getLayerBounds(LayerIndex layerIndex) const;
        void clearLayer(int layerIndex);
        void deleteLayer(LayerIndex layerIndex);

        void addMultiPoint(LayerIndex layerIndex, MultiPoint coords, picojson::value properties);
        void addMultiLineString(LayerIndex layerIndex, MultiLineString coordsList, picojson::value properties);
        void addMultiPolygon(LayerIndex layerIndex, MultiPolygon ringsList, picojson::value properties);

        void importGeoJSON(LayerIndex layerIndex, const picojson::value& geoJSON);
        void importGeoJSONFeatureCollection(LayerIndex layerIndex, const picojson::value& featureCollectionDef);
        void importGeoJSONFeature(LayerIndex layerIndex, const picojson::value& featureDef);

        void buildTile(int zoom, int tileX, int tileY, protobuf::encoded_message& encodedTile) const;
        void buildTiles(std::function<void(int, int, int, const protobuf::encoded_message&)> handler) const;

    private:
        using Geometry = std::variant<MultiPoint, MultiLineString, MultiPolygon>;

        struct Feature {
            std::uint64_t id = 0;
            Bounds bounds = Bounds::smallest(); // EPSG3856
            Geometry geometry; // EPSG3856
            picojson::value properties;
        };

        struct Layer {
            std::string name;
            Bounds bounds = Bounds::smallest(); // EPSG3856
            std::vector<Feature> features;
            float buffer = 0;
        };

        static constexpr double PI = boost::math::constants::pi<double>();
        static constexpr double EARTH_RADIUS = 6378137.0;
        static constexpr float TILE_SUBPIXEL_TOLERANCE_DIVIDER = 4.0f;
        static constexpr int TILE_PIXELS = 256;

        std::uint64_t extractFeatureId(LayerIndex layerIndex, const picojson::value& properties);

        const std::map<LayerIndex, Layer>& simplifyAndCacheLayers(int zoom) const;
        void invalidateCache() const;

        static void simplifyLayer(Layer& layer, double tolerance);
        static bool encodeLayer(const Layer& layer, const Point& tileOrigin, double tileSize, const Bounds& tileBounds, MBVTLayerEncoder& layerEncoder);

        static std::vector<std::vector<cglib::vec2<double>>> parseCoordinatesRings(const picojson::value& coordsDef);
        static std::vector<cglib::vec2<double>> parseCoordinatesList(const picojson::value& coordsDef);
        static cglib::vec2<double> parseCoordinates(const picojson::value& coordsDef);
        static Point wgs84ToWM(const cglib::vec2<double>& posWgs84);

        bool _fastSimplifyMode = false;
        float _simplifyTolerance = 1.0f;
        float _defaultLayerBuffer = 4.0f;

        std::map<LayerIndex, Layer> _layers;

        const int _minZoom;
        const int _maxZoom;

        mutable std::map<int, std::map<LayerIndex, Layer>> _cachedZoomLayers;

        mutable std::mutex _mutex;
    };
}

#endif
