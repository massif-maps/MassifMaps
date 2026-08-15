#include "MBVTTileBuilder.h"
#include "MBVTLayerEncoder.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

#include <mapbox/geojsonvt.hpp>

#include "mapnikvt/mbvtpackage/MBVTPackage.pb.h"

namespace massif::mbvtbuilder
{
    // geojson-vt's tile pyramid, driven by our own drill instead of its GeoJSONVT glue.
    //
    // Same algorithm - a tile is cut from the slice its parent already made, axis-separated so the
    // left half is clipped once and shared by both children - but four things differ, each of which
    // cost real time when measured against upstream's class on a device:
    //
    //  - the root is the deepest tile that contains the layer (upstream always roots at z0, so a
    //    city-sized layer paid ~9 levels of whole-dataset copies before the first real cut),
    //  - only the REQUESTED tile is turned into int16 tile coordinates; upstream builds that for
    //    every node it walks through,
    //  - nothing caches the built tile, because the SDK caches the encoded MVT above us,
    //  - splitting STOPS once nothing in the node is bigger than the tile being asked for, and
    //    that tile is cut straight out of the node. Splitting costs a pass over the whole node per
    //    level and only helps features that have to be CUT; one smaller than the tile is thrown out
    //    by the clipper's per-feature bbox test for two comparisons either way. Without this stop,
    //    5000 short routes cost 583 ms over 256 tiles at z14 where the old scan-every-feature
    //    builder took 209 ms. Note the test is against the TARGET zoom, not the next level down: a
    //    piece that fits one child can still span dozens of tiles at the zoom actually wanted, and
    //    stopping on it cost the long-route set 575 ms instead of 274 ms.
    struct MBVTTileIndex
    {
        using VTFeatures = mapbox::geojsonvt::detail::vt_features;

        double tolerance = 0; // unit-space tolerance the vertex importances were computed with
        double bufferFraction = 0; // tile buffer, as a fraction of a tile
        std::uint16_t extent = 0;
        int maxZoom = 0;

        int rootZoom = 0;
        std::uint32_t rootX = 0;
        std::uint32_t rootY = 0;

        struct Node
        {
            VTFeatures features;
            // Widest feature in the node, in unit space. Splitting only helps features bigger than
            // the tile being asked for; anything smaller is thrown out by the clipper's per-feature
            // bbox test for two comparisons either way. Kept per node so the test is O(1) instead
            // of a pass over every feature on every tile.
            double maxSpan = 0;
        };

        std::unordered_map<std::uint64_t, Node> nodes;
    };

    // The per-layer store. Geometry lives here in WGS84, exactly once: the tile index slices it on
    // demand, so a feature is walked once per LEVEL instead of once per tile.
    struct MBVTLayerData
    {
        struct FeatureInfo
        {
            std::uint64_t id = 0;
            picojson::value properties;
        };

        // Feature ids here are SLOT indices into infos, not the MVT feature ids: geojson-vt copies
        // the id into every clipped piece, and properties are kept out of the index entirely (a
        // shared empty map), so slicing never touches a picojson value.
        mapbox::feature::feature_collection<double> features;
        std::vector<FeatureInfo> infos;

        mutable std::unique_ptr<MBVTTileIndex> index;
    };

    struct MBVTFeatureGeometry
    {
        mapbox::geometry::geometry<double> value;
    };

    namespace
    {
        constexpr int TILE_EXTENT = 4096;

        // The pyramid keeps every slice it ever cut. Past this many nodes everything below the root
        // is dropped, which bounds a long panning session's memory.
        constexpr std::size_t MAX_INDEX_NODES = 8192;

        using VTPoint = mapbox::geometry::point<double>;
        using VTLinearRing = mapbox::geometry::linear_ring<double>;

        VTPoint convertPoint(const MBVTTileBuilder::Point &coords)
        {
            return VTPoint(coords(0), coords(1));
        }

        mapbox::geometry::multi_point<double> convertMultiPoint(const MBVTTileBuilder::MultiPoint &coords)
        {
            mapbox::geometry::multi_point<double> result;
            result.reserve(coords.size());
            for (const MBVTTileBuilder::Point &pos : coords)
            {
                result.push_back(convertPoint(pos));
            }
            return result;
        }

        mapbox::geometry::multi_line_string<double> convertMultiLineString(const MBVTTileBuilder::MultiLineString &coordsList)
        {
            mapbox::geometry::multi_line_string<double> result;
            result.reserve(coordsList.size());
            for (const std::vector<MBVTTileBuilder::Point> &coords : coordsList)
            {
                mapbox::geometry::line_string<double> line;
                line.reserve(coords.size());
                for (const MBVTTileBuilder::Point &pos : coords)
                {
                    line.push_back(convertPoint(pos));
                }
                result.push_back(std::move(line));
            }
            return result;
        }

        mapbox::geometry::multi_polygon<double> convertMultiPolygon(const MBVTTileBuilder::MultiPolygon &ringsList)
        {
            mapbox::geometry::multi_polygon<double> result;
            result.reserve(ringsList.size());
            for (const std::vector<std::vector<MBVTTileBuilder::Point>> &rings : ringsList)
            {
                mapbox::geometry::polygon<double> polygon;
                polygon.reserve(rings.size());
                for (const std::vector<MBVTTileBuilder::Point> &coords : rings)
                {
                    if (coords.empty())
                    {
                        continue;
                    }
                    VTLinearRing ring;
                    ring.reserve(coords.size() + 1);
                    for (const MBVTTileBuilder::Point &pos : coords)
                    {
                        ring.push_back(convertPoint(pos));
                    }
                    // The clipper and the ring area both walk closed rings; the old builder treated
                    // an open ring as implicitly closed, so close it here to keep the same shape.
                    if (!(ring.front() == ring.back()))
                    {
                        ring.push_back(ring.front());
                    }
                    polygon.push_back(std::move(ring));
                }
                result.push_back(std::move(polygon));
            }
            return result;
        }

        MBVTLayerEncoder::Point convertTilePoint(const mapbox::geometry::point<std::int16_t> &pos)
        {
            return MBVTLayerEncoder::Point(static_cast<float>(pos.x) / TILE_EXTENT, static_cast<float>(pos.y) / TILE_EXTENT);
        }

        // Encodes one geojson-vt tile feature. Points/lines/polygons each collapse to the single
        // MVT geometry type the encoder takes, as the old per-feature visitor did.
        struct TileFeatureEncoder
        {
            TileFeatureEncoder(std::uint64_t id, const picojson::value &properties, MBVTLayerEncoder &layerEncoder) : _id(id), _properties(properties), _layerEncoder(layerEncoder) {}

            bool operator()(const mapbox::geometry::point<std::int16_t> &pos) const
            {
                std::vector<MBVTLayerEncoder::Point> tileCoords { convertTilePoint(pos) };
                _layerEncoder.addMultiPoint(_id, tileCoords, _properties);
                return true;
            }

            bool operator()(const mapbox::geometry::multi_point<std::int16_t> &points) const
            {
                std::vector<MBVTLayerEncoder::Point> tileCoords;
                tileCoords.reserve(points.size());
                for (const auto &pos : points)
                {
                    tileCoords.push_back(convertTilePoint(pos));
                }
                if (tileCoords.empty())
                {
                    return false;
                }
                _layerEncoder.addMultiPoint(_id, tileCoords, _properties);
                return true;
            }

            bool operator()(const mapbox::geometry::line_string<std::int16_t> &line) const
            {
                return encodeLines({ line });
            }

            bool operator()(const mapbox::geometry::multi_line_string<std::int16_t> &lines) const
            {
                return encodeLines(lines);
            }

            bool operator()(const mapbox::geometry::polygon<std::int16_t> &polygon) const
            {
                return encodePolygons({ polygon });
            }

            bool operator()(const mapbox::geometry::multi_polygon<std::int16_t> &polygons) const
            {
                return encodePolygons(polygons);
            }

            template <typename Geometry>
            bool operator()(const Geometry &) const
            {
                return false; // empty and geometry collections carry nothing this builder produces
            }

        private:
            bool encodeLines(const mapbox::geometry::multi_line_string<std::int16_t> &lines) const
            {
                std::vector<std::vector<MBVTLayerEncoder::Point>> tileCoordsList;
                tileCoordsList.reserve(lines.size());
                for (const auto &line : lines)
                {
                    if (line.size() < 2)
                    {
                        continue;
                    }
                    std::vector<MBVTLayerEncoder::Point> tileCoords;
                    tileCoords.reserve(line.size());
                    for (const auto &pos : line)
                    {
                        tileCoords.push_back(convertTilePoint(pos));
                    }
                    tileCoordsList.push_back(std::move(tileCoords));
                }
                if (tileCoordsList.empty())
                {
                    return false;
                }
                _layerEncoder.addMultiLineString(_id, tileCoordsList, _properties);
                return true;
            }

            bool encodePolygons(const mapbox::geometry::multi_polygon<std::int16_t> &polygons) const
            {
                std::vector<std::vector<MBVTLayerEncoder::Point>> tileCoordsList;
                for (const auto &polygon : polygons)
                {
                    for (std::size_t i = 0; i < polygon.size(); i++)
                    {
                        const auto &ring = polygon[i];
                        if (ring.size() < 3)
                        {
                            continue;
                        }

                        std::vector<MBVTLayerEncoder::Point> tileCoords;
                        tileCoords.reserve(ring.size());
                        double signedArea = 0;
                        auto prevPos = ring.back();
                        for (const auto &pos : ring)
                        {
                            tileCoords.push_back(convertTilePoint(pos));
                            signedArea += static_cast<double>(prevPos.x) * pos.y - static_cast<double>(prevPos.y) * pos.x;
                            prevPos = pos;
                        }
                        // MVT wants the exterior ring wound one way and the holes the other.
                        if ((signedArea < 0) != (i > 0))
                        {
                            std::reverse(tileCoords.begin(), tileCoords.end());
                        }
                        tileCoordsList.push_back(std::move(tileCoords));
                    }
                }
                if (tileCoordsList.empty())
                {
                    return false;
                }
                _layerEncoder.addMultiPolygon(_id, tileCoordsList, _properties);
                return true;
            }

            const std::uint64_t _id;
            const picojson::value &_properties;
            MBVTLayerEncoder &_layerEncoder;
        };

        bool encodeTile(const MBVTLayerData &layerData, const mapbox::geojsonvt::Tile &tile, MBVTLayerEncoder &layerEncoder)
        {
            bool featuresAdded = false;
            for (const auto &feature : tile.features)
            {
                if (!feature.id.is<std::uint64_t>())
                {
                    continue;
                }
                std::size_t slot = static_cast<std::size_t>(feature.id.get<std::uint64_t>());
                if (slot >= layerData.infos.size())
                {
                    continue;
                }
                const MBVTLayerData::FeatureInfo &info = layerData.infos[slot];

                TileFeatureEncoder encoder(info.id, info.properties, layerEncoder);
                if (mapbox::util::apply_visitor(encoder, feature.geometry))
                {
                    featuresAdded = true;
                }
            }
            return featuresAdded;
        }

        mapbox::geojsonvt::TileOptions makeTileOptions(float simplifyTolerance, float layerBuffer, int tilePixels, float subpixelDivider)
        {
            mapbox::geojsonvt::TileOptions tileOptions;
            tileOptions.extent = TILE_EXTENT;
            // Same tolerance as before: a fraction of a tile PIXEL, so it holds constant on screen
            // across zooms - only the units change, geojson-vt measures it in tile extent units.
            tileOptions.tolerance = static_cast<double>(simplifyTolerance) * TILE_EXTENT / (tilePixels * subpixelDivider);
            tileOptions.buffer = static_cast<std::uint16_t>(std::max(0.0f, std::round(layerBuffer * TILE_EXTENT)));
            tileOptions.lineMetrics = false;
            return tileOptions;
        }

        // geojson-vt's own geoJSONToTile(), minus the geojson variant round-trip: convert, clip to
        // the tile, transform. No index, because a single-zoom builder has nothing to amortise.
        mapbox::geojsonvt::Tile buildSingleTile(const MBVTLayerData &data, const mapbox::geojsonvt::TileOptions &tileOptions, int zoom, int tileX, int tileY)
        {
            const double z2 = static_cast<double>(1u << zoom);
            const double tolerance = (tileOptions.tolerance / tileOptions.extent) / z2;
            const double p = static_cast<double>(tileOptions.buffer) / tileOptions.extent;

            auto features = mapbox::geojsonvt::detail::convert(data.features, tolerance, false);
            auto left = mapbox::geojsonvt::detail::clip<0>(features, (tileX - p) / z2, (tileX + 1 + p) / z2, -1, 2, false);
            auto clipped = mapbox::geojsonvt::detail::clip<1>(left, (tileY - p) / z2, (tileY + 1 + p) / z2, -1, 2, false);
            return mapbox::geojsonvt::detail::InternalTile({ clipped, static_cast<std::uint8_t>(zoom), static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), tileOptions.extent, tolerance, false }).tile;
        }

        MBVTTileIndex::Node makeNode(MBVTTileIndex::VTFeatures features)
        {
            MBVTTileIndex::Node node;
            for (const auto &feature : features)
            {
                node.maxSpan = std::max(node.maxSpan, std::max(feature.bbox.max.x - feature.bbox.min.x, feature.bbox.max.y - feature.bbox.min.y));
            }
            node.features = std::move(features);
            return node;
        }

        // The deepest tile whose BUFFERED extent still contains the whole layer. Any tile whose
        // buffered bounds touch the data is a descendant of it, so rooting the pyramid here loses
        // nothing and skips every level above, where clipping removes nothing and only copies.
        void findRootTile(const MBVTTileIndex::VTFeatures &features, double bufferFraction, int maxZoom, int &rootZoom, std::uint32_t &rootX, std::uint32_t &rootY)
        {
            rootZoom = 0;
            rootX = 0;
            rootY = 0;

            mapbox::geometry::box<double> bounds = { { 2, 2 }, { -1, -1 } };
            for (const auto &feature : features)
            {
                bounds.min.x = std::min(feature.bbox.min.x, bounds.min.x);
                bounds.min.y = std::min(feature.bbox.min.y, bounds.min.y);
                bounds.max.x = std::max(feature.bbox.max.x, bounds.max.x);
                bounds.max.y = std::max(feature.bbox.max.y, bounds.max.y);
            }
            if (bounds.min.x > bounds.max.x)
            {
                return;
            }

            for (int zoom = 1; zoom <= maxZoom; zoom++)
            {
                double z2 = static_cast<double>(1u << zoom);
                double x0 = std::floor(bounds.min.x * z2 - bufferFraction);
                double x1 = std::floor(bounds.max.x * z2 + bufferFraction);
                double y0 = std::floor(bounds.min.y * z2 - bufferFraction);
                double y1 = std::floor(bounds.max.y * z2 + bufferFraction);
                if (x0 != x1 || y0 != y1 || x0 < 0 || y0 < 0 || x0 >= z2 || y0 >= z2)
                {
                    break;
                }
                rootZoom = zoom;
                rootX = static_cast<std::uint32_t>(x0);
                rootY = static_cast<std::uint32_t>(y0);
            }
        }

        MBVTTileIndex &ensureIndex(const MBVTLayerData &data, const mapbox::geojsonvt::TileOptions &tileOptions, int maxZoom)
        {
            if (data.index && data.index->nodes.size() > MAX_INDEX_NODES)
            {
                // Keep the root - it is the whole converted dataset - and drop the slices.
                auto root = data.index->nodes.find(mapbox::geojsonvt::toID(static_cast<std::uint8_t>(data.index->rootZoom), data.index->rootX, data.index->rootY));
                MBVTTileIndex::Node rootNode = (root != data.index->nodes.end() ? std::move(root->second) : MBVTTileIndex::Node());
                data.index->nodes.clear();
                data.index->nodes.emplace(mapbox::geojsonvt::toID(static_cast<std::uint8_t>(data.index->rootZoom), data.index->rootX, data.index->rootY), std::move(rootNode));
            }
            if (!data.index)
            {

                auto index = std::make_unique<MBVTTileIndex>();
                index->extent = tileOptions.extent;
                index->maxZoom = std::min(maxZoom, 24);
                index->bufferFraction = static_cast<double>(tileOptions.buffer) / tileOptions.extent;
                // Vertex importances are computed once, against the tolerance of the DEEPEST zoom;
                // every tile then just filters on them, which is what removes the per-zoom pass.
                index->tolerance = (tileOptions.tolerance / tileOptions.extent) / static_cast<double>(1u << index->maxZoom);

                auto converted = mapbox::geojsonvt::detail::convert(data.features, index->tolerance, false);
                auto wrapped = mapbox::geojsonvt::detail::wrap(converted, index->bufferFraction, false);
                findRootTile(wrapped, index->bufferFraction, index->maxZoom, index->rootZoom, index->rootX, index->rootY);
                index->nodes.emplace(mapbox::geojsonvt::toID(static_cast<std::uint8_t>(index->rootZoom), index->rootX, index->rootY), makeNode(std::move(wrapped)));

                data.index = std::move(index);
            }
            return *data.index;
        }

        // Cuts the pyramid down towards (zoom, tileX, tileY), reusing whatever slice is cached and
        // stopping early on a small node. Returns the deepest node on the path, with its own tile
        // coordinates in nodeZoom/nodeX/nodeY - which is the wanted tile only when it drilled all
        // the way. Null means the tile holds none of the layer.
        const MBVTTileIndex::Node *drillToTile(MBVTTileIndex &index, int zoom, std::uint32_t tileX, std::uint32_t tileY, int &nodeZoom, std::uint32_t &nodeX, std::uint32_t &nodeY)
        {
            auto nodeAt = [&index](int z, std::uint32_t x, std::uint32_t y) -> const MBVTTileIndex::Node *
            {
                auto it = index.nodes.find(mapbox::geojsonvt::toID(static_cast<std::uint8_t>(z), x, y));
                return (it != index.nodes.end() ? &it->second : nullptr);
            };

            if (zoom <= index.rootZoom)
            {
                // The root, and so the whole layer, is inside this tile - or nowhere near it.
                int shift = index.rootZoom - zoom;
                if ((index.rootX >> shift) != tileX || (index.rootY >> shift) != tileY)
                {
                    return nullptr;
                }
                nodeZoom = index.rootZoom;
                nodeX = index.rootX;
                nodeY = index.rootY;
                return nodeAt(index.rootZoom, index.rootX, index.rootY);
            }

            if ((tileX >> (zoom - index.rootZoom)) != index.rootX || (tileY >> (zoom - index.rootZoom)) != index.rootY)
            {
                return nullptr; // not under the root: no data
            }

            // Deepest cached ancestor on the path down to the wanted tile.
            int ancestorZoom = zoom;
            while (ancestorZoom > index.rootZoom && !nodeAt(ancestorZoom, tileX >> (zoom - ancestorZoom), tileY >> (zoom - ancestorZoom)))
            {
                ancestorZoom--;
            }

            const double p = index.bufferFraction;
            for (int z = ancestorZoom; z < zoom; z++)
            {
                std::uint32_t x = tileX >> (zoom - z);
                std::uint32_t y = tileY >> (zoom - z);
                const MBVTTileIndex::Node *node = nodeAt(z, x, y);
                if (!node || node->features.empty())
                {
                    return nullptr;
                }
                // Nothing here is bigger than the tile being asked for: splitting further would
                // only copy features between levels, so cut the tile straight out of this node.
                if (node->maxSpan <= 1.0 / static_cast<double>(1u << zoom))
                {
                    nodeZoom = z;
                    nodeX = x;
                    nodeY = y;
                    return node;
                }

                const double z2 = static_cast<double>(1u << z);
                // Axis-separated, geojson-vt's way: one x pass feeds both of its children.
                auto left = mapbox::geojsonvt::detail::clip<0>(node->features, (x - p) / z2, (x + 0.5 + p) / z2, -1, 2, false);
                auto right = mapbox::geojsonvt::detail::clip<0>(node->features, (x + 0.5 - p) / z2, (x + 1 + p) / z2, -1, 2, false);

                index.nodes.emplace(mapbox::geojsonvt::toID(static_cast<std::uint8_t>(z + 1), x * 2, y * 2), makeNode(mapbox::geojsonvt::detail::clip<1>(left, (y - p) / z2, (y + 0.5 + p) / z2, -1, 2, false)));
                index.nodes.emplace(mapbox::geojsonvt::toID(static_cast<std::uint8_t>(z + 1), x * 2, y * 2 + 1), makeNode(mapbox::geojsonvt::detail::clip<1>(left, (y + 0.5 - p) / z2, (y + 1 + p) / z2, -1, 2, false)));
                index.nodes.emplace(mapbox::geojsonvt::toID(static_cast<std::uint8_t>(z + 1), x * 2 + 1, y * 2), makeNode(mapbox::geojsonvt::detail::clip<1>(right, (y - p) / z2, (y + 0.5 + p) / z2, -1, 2, false)));
                index.nodes.emplace(mapbox::geojsonvt::toID(static_cast<std::uint8_t>(z + 1), x * 2 + 1, y * 2 + 1), makeNode(mapbox::geojsonvt::detail::clip<1>(right, (y + 0.5 - p) / z2, (y + 1 + p) / z2, -1, 2, false)));
            }

            nodeZoom = zoom;
            nodeX = tileX;
            nodeY = tileY;
            const MBVTTileIndex::Node *node = nodeAt(zoom, tileX, tileY);
            return (node && !node->features.empty() ? node : nullptr);
        }

        mapbox::geojsonvt::Tile buildIndexedTile(MBVTTileIndex &index, int zoom, int tileX, int tileY)
        {
            int nodeZoom = 0;
            std::uint32_t nodeX = 0, nodeY = 0;
            const MBVTTileIndex::Node *node = drillToTile(index, zoom, static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), nodeZoom, nodeX, nodeY);
            if (!node)
            {
                return mapbox::geojsonvt::Tile();
            }

            const double tolerance = index.tolerance * static_cast<double>(1u << (index.maxZoom - zoom));
            if (nodeZoom == zoom)
            {
                return mapbox::geojsonvt::detail::InternalTile({ node->features, static_cast<std::uint8_t>(zoom), static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), index.extent, tolerance, false }).tile;
            }

            // Stopped on a coarser node: cut the wanted tile straight out of it. Not cached - the
            // SDK caches the encoded tile, and caching this would grow the index by a node per tile
            // for exactly the datasets the stop exists to keep cheap.
            const double z2 = static_cast<double>(1u << zoom);
            const double p = index.bufferFraction;
            const double minX = (tileX - p) / z2, maxX = (tileX + 1 + p) / z2;
            const double minY = (tileY - p) / z2, maxY = (tileY + 1 + p) / z2;

            // Pick the features that touch the tile FIRST. geojson-vt's clip reserves its output for
            // the whole input, so running it straight over a coarse node allocates for every feature
            // in the layer on every tile - which is what made serving from a coarse node cost more
            // than the old full scan it was meant to replace.
            MBVTTileIndex::VTFeatures candidates;
            for (const auto &feature : node->features)
            {
                if (feature.bbox.max.x < minX || feature.bbox.min.x >= maxX || feature.bbox.max.y < minY || feature.bbox.min.y >= maxY)
                {
                    continue;
                }
                candidates.push_back(feature);
            }
            if (candidates.empty())
            {
                return mapbox::geojsonvt::Tile();
            }

            auto left = mapbox::geojsonvt::detail::clip<0>(candidates, minX, maxX, -1, 2, false);
            auto clipped = mapbox::geojsonvt::detail::clip<1>(left, minY, maxY, -1, 2, false);
            return mapbox::geojsonvt::detail::InternalTile({ clipped, static_cast<std::uint8_t>(zoom), static_cast<std::uint32_t>(tileX), static_cast<std::uint32_t>(tileY), index.extent, tolerance, false }).tile;
        }
    }

    MBVTTileBuilder::MBVTTileBuilder(int minZoom, int maxZoom) : _minZoom(minZoom), _maxZoom(maxZoom)
    {
    }

    float MBVTTileBuilder::getSimplifyTolerance() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _simplifyTolerance;
    }

    void MBVTTileBuilder::setSimplifyTolerance(float tolerance)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _simplifyTolerance = tolerance;
        invalidateCache();
    }

    float MBVTTileBuilder::getDefaultLayerBuffer() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _defaultLayerBuffer;
    }

    void MBVTTileBuilder::setDefaultLayerBuffer(float buffer)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _defaultLayerBuffer = buffer;
    }

    std::vector<MBVTTileBuilder::LayerIndex> MBVTTileBuilder::getLayerIndices() const
    {
        std::vector<LayerIndex> layerIndices;
        for (auto it = _layers.begin(); it != _layers.end(); it++)
        {
            layerIndices.push_back(it->first);
        }
        return layerIndices;
    }

    MBVTTileBuilder::LayerIndex MBVTTileBuilder::createLayer(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Layer layer;
        layer.name = name;
        layer.buffer = _defaultLayerBuffer / TILE_PIXELS;
        layer.data = std::make_shared<MBVTLayerData>();
        LayerIndex layerIndex = (_layers.empty() ? 0 : _layers.rbegin()->first) + 1;
        _layers.emplace(layerIndex, std::move(layer));
        invalidateCache();
        return layerIndex;
    }

    MBVTTileBuilder::Bounds MBVTTileBuilder::getLayerBounds(LayerIndex layerIndex) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _layers.find(layerIndex);
        if (it == _layers.end())
        {
            throw std::runtime_error("Invalid layer index");
        }
        return it->second.bounds;
    }

    void MBVTTileBuilder::clearLayer(LayerIndex layerIndex)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Layer &layer = _layers[layerIndex];
        if (!layer.data)
        {
            layer.data = std::make_shared<MBVTLayerData>();
        }
        layer.bounds = Bounds::smallest();
        layer.data->features.clear();
        layer.data->infos.clear();
        invalidateCache();
    }

    void MBVTTileBuilder::deleteLayer(LayerIndex layerIndex)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _layers.find(layerIndex);
        if (it == _layers.end())
        {
            throw std::runtime_error("Invalid layer index");
        }
        _layers.erase(it);
        invalidateCache();
    }

    void MBVTTileBuilder::addMultiPoint(LayerIndex layerIndex, MultiPoint coords, picojson::value id, picojson::value properties, const bool updateOnly)
    {
        Bounds bounds = Bounds::smallest();
        for (const Point &pos : coords)
        {
            bounds.add(wgs84ToWM(pos));
        }
        MBVTFeatureGeometry geometry { convertMultiPoint(coords) };
        storeFeature(layerIndex, geometry, bounds, std::move(id), std::move(properties), updateOnly);
    }

    void MBVTTileBuilder::addMultiLineString(LayerIndex layerIndex, MultiLineString coordsList, picojson::value id, picojson::value properties, const bool updateOnly)
    {
        Bounds bounds = Bounds::smallest();
        for (const std::vector<Point> &coords : coordsList)
        {
            for (const Point &pos : coords)
            {
                bounds.add(wgs84ToWM(pos));
            }
        }
        MBVTFeatureGeometry geometry { convertMultiLineString(coordsList) };
        storeFeature(layerIndex, geometry, bounds, std::move(id), std::move(properties), updateOnly);
    }

    void MBVTTileBuilder::addMultiPolygon(LayerIndex layerIndex, MultiPolygon ringsList, picojson::value id, picojson::value properties, const bool updateOnly)
    {
        Bounds bounds = Bounds::smallest();
        for (const std::vector<std::vector<Point>> &rings : ringsList)
        {
            for (const std::vector<Point> &coords : rings)
            {
                for (const Point &pos : coords)
                {
                    bounds.add(wgs84ToWM(pos));
                }
            }
        }
        MBVTFeatureGeometry geometry { convertMultiPolygon(ringsList) };
        storeFeature(layerIndex, geometry, bounds, std::move(id), std::move(properties), updateOnly);
    }

    void MBVTTileBuilder::storeFeature(LayerIndex layerIndex, MBVTFeatureGeometry &geometry, const Bounds &bounds, picojson::value id, picojson::value properties, const bool updateOnly)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Layer &layer = _layers[layerIndex];
        if (!layer.data)
        {
            layer.data = std::make_shared<MBVTLayerData>();
        }
        MBVTLayerData &data = *layer.data;

        std::uint64_t actualId = extractFeatureId(layerIndex, id, properties);
        if (updateOnly)
        {
            auto it = std::find_if(data.infos.begin(), data.infos.end(), [&actualId](const MBVTLayerData::FeatureInfo &info) -> bool
            {
                return info.id == actualId;
            });
            if (it == data.infos.end())
            {
                return;
            }
            // NOTE: as before, the layer bounds are not shrunk back - they would have to be
            // recomputed from every remaining feature.
            std::size_t slot = static_cast<std::size_t>(it - data.infos.begin());
            data.infos.erase(it);
            data.features.erase(data.features.begin() + slot);
            restampFeatureSlots(data);
        }

        MBVTLayerData::FeatureInfo info;
        info.id = actualId;
        info.properties = std::move(properties);

        mapbox::feature::feature<double> feature(std::move(geometry.value));
        feature.id = static_cast<std::uint64_t>(data.infos.size());

        data.infos.push_back(std::move(info));
        data.features.push_back(std::move(feature));
        layer.bounds.add(bounds);
        invalidateCache();
    }

    void MBVTTileBuilder::restampFeatureSlots(MBVTLayerData &data)
    {
        for (std::size_t i = 0; i < data.features.size(); i++)
        {
            data.features[i].id = static_cast<std::uint64_t>(i);
        }
    }

    void MBVTTileBuilder::importGeoJSON(LayerIndex layerIndex, const picojson::value &geoJSON)
    {
        std::string type = geoJSON.get("type").get<std::string>();
        if (type == "FeatureCollection")
        {
            importGeoJSONFeatureCollection(layerIndex, geoJSON);
        }
        else if (type == "Feature")
        {
            importGeoJSONFeature(layerIndex, geoJSON, false);
        }
        else
        {
            throw std::runtime_error("Unexpected element type");
        }
    }

    void MBVTTileBuilder::importGeoJSONFeatureCollection(LayerIndex layerIndex, const picojson::value &featureCollectionDef)
    {
        const picojson::array &featuresDef = featureCollectionDef.get("features").get<picojson::array>();

        for (const picojson::value &featureDef : featuresDef)
        {
            std::string type = featureDef.get("type").get<std::string>();
            if (type != "Feature")
            {
                throw std::runtime_error("Unexpected element type");
            }

            importGeoJSONFeature(layerIndex, featureDef, false);
        }
    }

    void MBVTTileBuilder::importGeoJSONFeature(LayerIndex layerIndex, const picojson::value &featureDef, const bool updateOnly)
    {
        const picojson::value &geometryDef = featureDef.get("geometry");
        if (geometryDef.is<picojson::null>())
        {
            return;
        }
        const picojson::value &id = featureDef.get("id");
        const picojson::value &properties = featureDef.get("properties");

        std::string type = geometryDef.get("type").get<std::string>();
        const picojson::value &coordsDef = geometryDef.get("coordinates");

        if (type == "Point")
        {
            addMultiPoint(layerIndex, {parseCoordinates(coordsDef)}, id, properties, updateOnly);
        }
        else if (type == "LineString")
        {
            addMultiLineString(layerIndex, {parseCoordinatesList(coordsDef)}, id, properties, updateOnly);
        }
        else if (type == "Polygon")
        {
            addMultiPolygon(layerIndex, {parseCoordinatesRings(coordsDef)}, id, properties, updateOnly);
        }
        else if (type == "MultiPoint")
        {
            std::vector<cglib::vec2<double>> coords;
            for (const picojson::value &subCoordsDef : coordsDef.get<picojson::array>())
            {
                coords.push_back(parseCoordinates(subCoordsDef));
            }
            addMultiPoint(layerIndex, std::move(coords), id, properties, updateOnly);
        }
        else if (type == "MultiLineString")
        {
            std::vector<std::vector<cglib::vec2<double>>> coordsList;
            for (const picojson::value &subCoordsDef : coordsDef.get<picojson::array>())
            {
                coordsList.push_back(parseCoordinatesList(subCoordsDef));
            }
            addMultiLineString(layerIndex, std::move(coordsList), id, properties, updateOnly);
        }
        else if (type == "MultiPolygon")
        {
            std::vector<std::vector<std::vector<cglib::vec2<double>>>> ringsList;
            for (const picojson::value &subCoordsDef : coordsDef.get<picojson::array>())
            {
                ringsList.push_back(parseCoordinatesRings(subCoordsDef));
            }
            addMultiPolygon(layerIndex, std::move(ringsList), id, properties, updateOnly);
        }
        else
        {
            throw std::runtime_error("Invalid geometry type");
        }
    }

    void MBVTTileBuilder::removeGeoJSONFeature(LayerIndex layerIndex, const std::uint64_t id)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Layer &layer = _layers[layerIndex];
        if (!layer.data)
        {
            return;
        }
        MBVTLayerData &data = *layer.data;

        bool removed = false;
        for (std::size_t i = data.infos.size(); i > 0; i--)
        {
            if (data.infos[i - 1].id == id)
            {
                data.infos.erase(data.infos.begin() + (i - 1));
                data.features.erase(data.features.begin() + (i - 1));
                removed = true;
            }
        }
        if (removed)
        {
            restampFeatureSlots(data);
            invalidateCache();
            // NOTE: as before, the layer bounds are not shrunk back.
        }
    }

    void MBVTTileBuilder::buildTile(int zoom, int tileX, int tileY, protobuf::encoded_message &encodedTile) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        buildTileLayers(zoom, tileX, tileY, encodedTile);

    }

    bool MBVTTileBuilder::buildTileLayers(int zoom, int tileX, int tileY, protobuf::encoded_message &encodedTile) const
    {
        if (zoom < 0 || zoom > _maxZoom)
        {
            return false;
        }

        bool tileWritten = false;
        for (auto it = _layers.begin(); it != _layers.end(); it++)
        {
            const Layer &layer = it->second;
            if (!layer.data || layer.data->features.empty())
            {
                continue;
            }

            MBVTLayerEncoder layerEncoder(layer.name);
            mapbox::geojsonvt::TileOptions tileOptions = makeTileOptions(_simplifyTolerance, layer.buffer, TILE_PIXELS, TILE_SUBPIXEL_TOLERANCE_DIVIDER);
            bool featuresAdded = false;
            if (_minZoom == _maxZoom)
            {
                // A builder pinned to one zoom (a contour tile, say) only ever yields a single tile,
                // so there is nothing for an index to amortise: cut that tile directly.
                mapbox::geojsonvt::Tile tile = buildSingleTile(*layer.data, tileOptions, zoom, tileX, tileY);
                featuresAdded = encodeTile(*layer.data, tile, layerEncoder);
            }
            else
            {
                MBVTTileIndex &index = ensureIndex(*layer.data, tileOptions, _maxZoom);
                featuresAdded = encodeTile(*layer.data, buildIndexedTile(index, zoom, tileX, tileY), layerEncoder);
            }

            if (featuresAdded)
            {
                encodedTile.write_tag(vector_tile::Tile::kLayersFieldNumber, protobuf::encoded_message::length_type);
                encodedTile.write_message(layerEncoder.buildLayer());
                tileWritten = true;
            }
        }
        return tileWritten;
    }

    void MBVTTileBuilder::buildTiles(std::function<void(int, int, int, const protobuf::encoded_message &)> handler) const
    {
        static const Bounds mapBounds(Point(-PI * EARTH_RADIUS, -PI * EARTH_RADIUS), Point(PI * EARTH_RADIUS, PI * EARTH_RADIUS));

        std::lock_guard<std::mutex> lock(_mutex);
        for (int zoom = _maxZoom; zoom >= _minZoom; zoom--)
        {
            Bounds layersBounds = Bounds::smallest();
            for (auto it = _layers.begin(); it != _layers.end(); it++)
            {
                const Layer &layer = it->second;
                if (!layer.data || layer.data->features.empty())
                {
                    continue;
                }
                layersBounds.add(layer.bounds.min - cglib::vec2<double>(layer.buffer, layer.buffer));
                layersBounds.add(layer.bounds.max + cglib::vec2<double>(layer.buffer, layer.buffer));
            }
            if (layersBounds == Bounds::smallest())
            {
                continue;
            }

            double tileSize = (mapBounds.max(0) - mapBounds.min(0)) / (1 << zoom);
            double tileCount = (1 << zoom);

            double tileX0 = std::max(0.0, std::floor((layersBounds.min(0) - mapBounds.min(0)) / tileSize));
            double tileY0 = std::max(0.0, std::floor((layersBounds.min(1) - mapBounds.min(1)) / tileSize));
            double tileX1 = std::min(tileCount, std::floor((layersBounds.max(0) - mapBounds.min(0)) / tileSize) + 1);
            double tileY1 = std::min(tileCount, std::floor((layersBounds.max(1) - mapBounds.min(1)) / tileSize) + 1);

            for (int tileY = static_cast<int>(tileY0); tileY < tileY1; tileY++)
            {
                for (int tileX = static_cast<int>(tileX0); tileX < tileX1; tileX++)
                {
                    protobuf::encoded_message encodedTile;
                    if (buildTileLayers(zoom, tileX, tileY, encodedTile) && !encodedTile.empty())
                    {
                        handler(zoom, tileX, tileY, encodedTile);
                    }
                }
            }
        }
    }

    void MBVTTileBuilder::invalidateCache() const
    {
        for (auto it = _layers.begin(); it != _layers.end(); it++)
        {
            if (it->second.data)
            {
                it->second.data->index.reset();
            }
        }
    }

    std::uint64_t MBVTTileBuilder::extractFeatureId(LayerIndex layerIndex, picojson::value id, const picojson::value &properties)
    {
        picojson::value actualId = id;
        if (actualId.is<picojson::null>() && properties.is<picojson::object>() && properties.contains("id"))
        {
            actualId = properties.get("id");
        }
        if (actualId.is<std::int64_t>())
        {
            return actualId.get<std::int64_t>();
        }
        if (actualId.is<std::string>())
        {
            return std::hash<std::string>()(actualId.get<std::string>());
        }

        auto it = _layers.find(layerIndex);
        std::size_t size = (it != _layers.end() && it->second.data ? it->second.data->infos.size() : 0);
        return static_cast<std::uint64_t>(((layerIndex + 1ULL) << 32) + size);
    }

    std::vector<std::vector<MBVTTileBuilder::Point>> MBVTTileBuilder::parseCoordinatesRings(const picojson::value &coordsDef)
    {
        const picojson::array &coordsArray = coordsDef.get<picojson::array>();

        std::vector<std::vector<Point>> rings;
        rings.reserve(coordsArray.size());
        for (std::size_t i = 0; i < coordsArray.size(); i++)
        {
            std::vector<Point> coordsList = parseCoordinatesList(coordsArray[i]);
            rings.push_back(std::move(coordsList));
        }
        return rings;
    }

    std::vector<MBVTTileBuilder::Point> MBVTTileBuilder::parseCoordinatesList(const picojson::value &coordsDef)
    {
        const picojson::array &coordsArray = coordsDef.get<picojson::array>();

        std::vector<Point> coordsList;
        coordsList.reserve(coordsArray.size());
        for (std::size_t i = 0; i < coordsArray.size(); i++)
        {
            Point coords = parseCoordinates(coordsArray[i]);
            coordsList.push_back(coords);
        }
        return coordsList;
    }

    MBVTTileBuilder::Point MBVTTileBuilder::parseCoordinates(const picojson::value &coordsDef)
    {
        const picojson::array &coordsArray = coordsDef.get<picojson::array>();
        return cglib::vec2<double>(coordsArray.at(0).get<double>(), coordsArray.at(1).get<double>());
    }

    MBVTTileBuilder::Point MBVTTileBuilder::wgs84ToWM(const cglib::vec2<double> &posWgs84)
    {
        double x = EARTH_RADIUS * posWgs84(0) * PI / 180.0;
        double a = posWgs84(1) * PI / 180.0;
        double y = 0.5 * EARTH_RADIUS * std::log((1.0 + std::sin(a)) / (1.0 - std::sin(a)));
        return Point(x, -y); // NOTE: we use EPSG3857 with flipped Y
    }
}
