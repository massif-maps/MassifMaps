/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MBVTBUILDER_MBVTTILEBUILDER_H_
#define _MASSIF_MBVTBUILDER_MBVTTILEBUILDER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/math/constants/constants.hpp>

#include <cglib/vec.h>
#include <cglib/bbox.h>

#include <picojson/picojson.h>

#include <protobuf/encodedpbf.hpp>

namespace massif::mbvtbuilder {
    class MBVTLayerEncoder;

    // The per-layer geometry store and its geojson-vt tile index, and a wrapper around one feature's
    // geometry. Both are defined in the .cpp so that the mapbox headers stay out of every
    // translation unit that includes this one.
    struct MBVTLayerData;
    struct MBVTFeatureGeometry;

    class MBVTTileBuilder final {
    public:
        using LayerIndex = int;

        using Bounds = cglib::bbox2<double>;
        using Point = cglib::vec2<double>;
        using MultiPoint = std::vector<Point>;
        using MultiLineString = std::vector<std::vector<Point>>;
        using MultiPolygon = std::vector<std::vector<std::vector<Point>>>;

        explicit MBVTTileBuilder(int minZoom, int maxZoom);

        float getSimplifyTolerance() const;
        void setSimplifyTolerance(float tolerance);

        float getDefaultLayerBuffer() const;
        void setDefaultLayerBuffer(float buffer);

        std::vector<LayerIndex> getLayerIndices() const;
        LayerIndex createLayer(const std::string& name);
        Bounds getLayerBounds(LayerIndex layerIndex) const;
        void clearLayer(int layerIndex);
        void deleteLayer(LayerIndex layerIndex);

        void addMultiPoint(LayerIndex layerIndex, MultiPoint coords, picojson::value id, picojson::value properties, const bool updateOnly);
        void addMultiLineString(LayerIndex layerIndex, MultiLineString coordsList, picojson::value id, picojson::value properties, const bool updateOnly);
        void addMultiPolygon(LayerIndex layerIndex, MultiPolygon ringsList, picojson::value id, picojson::value properties, const bool updateOnly);

        void removeGeoJSONFeature(LayerIndex layerIndex, const std::uint64_t id);

        void importGeoJSON(LayerIndex layerIndex, const picojson::value& geoJSON);
        void importGeoJSONFeatureCollection(LayerIndex layerIndex, const picojson::value& featureCollectionDef);
        void importGeoJSONFeature(LayerIndex layerIndex, const picojson::value& featureDef, const bool updateOnly);

        void buildTile(int zoom, int tileX, int tileY, protobuf::encoded_message& encodedTile) const;
        void buildTiles(std::function<void(int, int, int, const protobuf::encoded_message&)> handler) const;

    private:
        struct Layer {
            std::string name;
            Bounds bounds = Bounds::smallest(); // EPSG3857
            float buffer = 0; // fraction of a tile
            std::shared_ptr<MBVTLayerData> data;
        };

        static constexpr double PI = boost::math::constants::pi<double>();
        static constexpr double EARTH_RADIUS = 6378137.0;
        static constexpr float TILE_SUBPIXEL_TOLERANCE_DIVIDER = 4.0f;
        static constexpr int TILE_PIXELS = 256;

        std::uint64_t extractFeatureId(LayerIndex layerIndex, picojson::value id, const picojson::value& properties);

        void storeFeature(LayerIndex layerIndex, MBVTFeatureGeometry& geometry, const Bounds& bounds, picojson::value id, picojson::value properties, const bool updateOnly);
        static void restampFeatureSlots(MBVTLayerData& data);

        // Drops every layer's tile index; the next buildTile rebuilds the ones it needs.
        void invalidateCache() const;

        bool buildTileLayers(int zoom, int tileX, int tileY, protobuf::encoded_message& encodedTile) const;

        static std::vector<std::vector<cglib::vec2<double>>> parseCoordinatesRings(const picojson::value& coordsDef);
        static std::vector<cglib::vec2<double>> parseCoordinatesList(const picojson::value& coordsDef);
        static cglib::vec2<double> parseCoordinates(const picojson::value& coordsDef);
        static Point wgs84ToWM(const cglib::vec2<double>& posWgs84);

        float _simplifyTolerance = 1.0f;
        float _defaultLayerBuffer = 4.0f;

        std::map<LayerIndex, Layer> _layers;

        const int _minZoom;
        const int _maxZoom;

        mutable std::mutex _mutex;
    };
}

#endif
