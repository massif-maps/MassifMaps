#include "GeoJSONVectorTileDataSource.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "components/Exceptions.h"
#include "geometry/Geometry.h"
#include "geometry/PointGeometry.h"
#include "geometry/LineGeometry.h"
#include "geometry/PolygonGeometry.h"
#include "geometry/MultiGeometry.h"
#include "geometry/MultiPointGeometry.h"
#include "geometry/MultiLineGeometry.h"
#include "geometry/MultiPolygonGeometry.h"
#include "geometry/FeatureCollection.h"
#include "projections/Projection.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <mbvtbuilder/MBVTTileBuilder.h>

#include <mapnikvt/mbvtpackage/MBVTPackage.pb.h>

namespace
{

    massif::mbvtbuilder::MBVTTileBuilder::Point convertPoint(const std::shared_ptr<massif::Projection> &projection, const massif::MapPos &mapPos)
    {
        massif::MapPos wgs84Pos = projection ? projection->toWgs84(mapPos) : mapPos;
        return massif::mbvtbuilder::MBVTTileBuilder::Point(wgs84Pos.getX(), wgs84Pos.getY());
    }

    std::vector<massif::mbvtbuilder::MBVTTileBuilder::Point> convertPoints(const std::shared_ptr<massif::Projection> &projection, const std::vector<massif::MapPos> &mapPoses)
    {
        std::vector<massif::mbvtbuilder::MBVTTileBuilder::Point> points;
        points.reserve(mapPoses.size());
        for (const massif::MapPos &mapPos : mapPoses)
        {
            points.push_back(convertPoint(projection, mapPos));
        }
        return points;
    }

    std::vector<std::vector<massif::mbvtbuilder::MBVTTileBuilder::Point>> convertPointsList(const std::shared_ptr<massif::Projection> &projection, const std::vector<std::vector<massif::MapPos>> &mapPosesList)
    {
        std::vector<std::vector<massif::mbvtbuilder::MBVTTileBuilder::Point>> pointsList;
        pointsList.reserve(mapPosesList.size());
        for (const std::vector<massif::MapPos> &mapPoses : mapPosesList)
        {
            pointsList.push_back(convertPoints(projection, mapPoses));
        }
        return pointsList;
    }

}

namespace massif
{

    GeoJSONVectorTileDataSource::GeoJSONVectorTileDataSource(int minZoom, int maxZoom) : TileDataSource(minZoom, maxZoom),
                                                                                         _tileBuilder(std::make_unique<mbvtbuilder::MBVTTileBuilder>(minZoom, maxZoom)),
                                                                                         _mutex()
    {
    }

    GeoJSONVectorTileDataSource::~GeoJSONVectorTileDataSource()
    {
    }

    float GeoJSONVectorTileDataSource::getSimplifyTolerance() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tileBuilder->getSimplifyTolerance();
    }

    void GeoJSONVectorTileDataSource::setSimplifyTolerance(float tolerance)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->setSimplifyTolerance(tolerance);
        }
        notifyTilesChanged(false);
    }

    float GeoJSONVectorTileDataSource::getDefaultLayerBuffer() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tileBuilder->getDefaultLayerBuffer();
    }

    void GeoJSONVectorTileDataSource::setDefaultLayerBuffer(float buffer)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _tileBuilder->setDefaultLayerBuffer(buffer);
    }

    int GeoJSONVectorTileDataSource::createLayer(const std::string &name)
    {
        int layerIndex = -1;
        try
        {
            std::lock_guard<std::mutex> lock(_mutex);
            layerIndex = _tileBuilder->createLayer(name);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::createLayer: Failed to create layer: %s", ex.what());
            throw GenericException("Failed to create layer", ex.what());
        }
        notifyTilesChanged(false);
        return layerIndex;
    }

    void GeoJSONVectorTileDataSource::setLayerGeoJSON(int layerIndex, const Variant &geoJSON)
    {
        try
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->clearLayer(layerIndex);
            _tileBuilder->importGeoJSON(layerIndex, geoJSON.toPicoJSON());
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::setLayerGeoJSON: Failed to update layer: %s", ex.what());
            throw GenericException("Failed to set layer contents", ex.what());
        }
        notifyTilesChanged(false);
    }

    void GeoJSONVectorTileDataSource::addGeoJSONFeature(int layerIndex, const Variant &geoJSON)
    {
        try
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->importGeoJSONFeature(layerIndex, geoJSON.toPicoJSON(), false);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::addGeoJSONFeature: Failed to update layer: %s", ex.what());
            throw GenericException("Failed to set layer contents", ex.what());
        }
        notifyTilesChanged(false);
    }

    void GeoJSONVectorTileDataSource::updateGeoJSONFeature(int layerIndex, const Variant &geoJSON)
    {
        try
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->importGeoJSONFeature(layerIndex, geoJSON.toPicoJSON(), true);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::updateGeoJSONFeature: Failed to update layer: %s", ex.what());
            throw GenericException("Failed to set layer contents", ex.what());
        }
        notifyTilesChanged(false);
    }

    void GeoJSONVectorTileDataSource::setLayerGeoJSONString(int layerIndex, const std::string &geoJSON)
    {
        try
        {
            picojson::value val;
            std::string err = picojson::parse(val, geoJSON);
            if (!err.empty())
            {
                throw ParseException(std::string("GeoJSON parsing failed: ") + err, geoJSON);
            }
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->clearLayer(layerIndex);
            _tileBuilder->importGeoJSON(layerIndex, val);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::setLayerGeoJSONString: Failed to update layer: %s", ex.what());
            throw GenericException("Failed to set layer contents", ex.what());
        }
        notifyTilesChanged(false);
    }

    void GeoJSONVectorTileDataSource::addGeoJSONStringFeature(int layerIndex, const std::string &geoJSON)
    {
        try
        {
            picojson::value val;
            std::string err = picojson::parse(val, geoJSON);
            if (!err.empty())
            {
                throw ParseException(std::string("GeoJSON parsing failed: ") + err, geoJSON);
            }
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->importGeoJSONFeature(layerIndex, val, false);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::addGeoJSONStringFeature: Failed to update layer: %s", ex.what());
            throw GenericException("Failed to set layer contents", ex.what());
        }
        notifyTilesChanged(false);
    }

    void GeoJSONVectorTileDataSource::updateGeoJSONStringFeature(int layerIndex, const std::string &geoJSON)
    {
        try
        {
            picojson::value val;
            std::string err = picojson::parse(val, geoJSON);
            if (!err.empty())
            {
                throw ParseException(std::string("GeoJSON parsing failed: ") + err, geoJSON);
            }
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->importGeoJSONFeature(layerIndex, val, true);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::updateGeoJSONStringFeature: Failed to update layer: %s", ex.what());
            throw GenericException("Failed to set layer contents", ex.what());
        }
        notifyTilesChanged(false);
    }

    void GeoJSONVectorTileDataSource::removeGeoJSONFeature(int layerIndex, const Variant &id)
    {
        try
        {
            std::lock_guard<std::mutex> lock(_mutex);
                picojson::value idValue = id.toPicoJSON();
                std::uint64_t idInt;
            if (idValue.is<std::int64_t>())
            {
                idInt =  idValue.get<std::int64_t>();
            }
            else if (idValue.is<std::string>())
            {
                idInt = std::hash<std::string>()(idValue.get<std::string>());
            } else {
                throw ParseException(std::string("removeGeoJSONFeature failed: id must be a string or a number Variant"));

            }
            _tileBuilder->removeGeoJSONFeature(layerIndex, idInt);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::removeGeoJSONFeature: Failed to update layer: %s", ex.what());
            throw GenericException("Failed to set layer contents", ex.what());
        }
        notifyTilesChanged(false);
    }

    void GeoJSONVectorTileDataSource::setLayerFeatureCollection(int layerIndex, const std::shared_ptr<Projection> &projection, const std::shared_ptr<FeatureCollection> &featureCollection)
    {
        if (!featureCollection)
        {
            throw NullArgumentException("Null featureCollection");
        }

        try
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->clearLayer(layerIndex);
            for (int n = 0; n < featureCollection->getFeatureCount(); n++)
            {
                const std::shared_ptr<Feature> &feature = featureCollection->getFeature(n);
                const std::shared_ptr<Geometry> &geometry = feature->getGeometry();
                // Feature does not have a id member like GeoJSON specs so i cant pass it here
                picojson::value id;
                picojson::value properties = feature->getProperties().toPicoJSON();

                if (auto point = std::dynamic_pointer_cast<PointGeometry>(geometry))
                {
                    mbvtbuilder::MBVTTileBuilder::MultiPoint points = {convertPoint(projection, point->getPos())};
                    _tileBuilder->addMultiPoint(layerIndex, std::move(points), id, std::move(properties), false);
                }
                else if (auto line = std::dynamic_pointer_cast<LineGeometry>(geometry))
                {
                    mbvtbuilder::MBVTTileBuilder::MultiLineString lines = {convertPoints(projection, line->getPoses())};
                    _tileBuilder->addMultiLineString(layerIndex, std::move(lines), id, std::move(properties), false);
                }
                else if (auto polygon = std::dynamic_pointer_cast<PolygonGeometry>(geometry))
                {
                    mbvtbuilder::MBVTTileBuilder::MultiPolygon polygons = {convertPointsList(projection, polygon->getRings())};
                    _tileBuilder->addMultiPolygon(layerIndex, std::move(polygons), id, std::move(properties), false);
                }
                else if (auto multiPoint = std::dynamic_pointer_cast<MultiPointGeometry>(geometry))
                {
                    mbvtbuilder::MBVTTileBuilder::MultiPoint points;
                    points.reserve(multiPoint->getGeometryCount());
                    for (int i = 0; i < multiPoint->getGeometryCount(); i++)
                    {
                        points.push_back(convertPoint(projection, multiPoint->getGeometry(i)->getPos()));
                    }
                    _tileBuilder->addMultiPoint(layerIndex, std::move(points), id, std::move(properties), false);
                }
                else if (auto multiLine = std::dynamic_pointer_cast<MultiLineGeometry>(geometry))
                {
                    mbvtbuilder::MBVTTileBuilder::MultiLineString lines;
                    lines.reserve(multiLine->getGeometryCount());
                    for (int i = 0; i < multiLine->getGeometryCount(); i++)
                    {
                        lines.push_back(convertPoints(projection, multiLine->getGeometry(i)->getPoses()));
                    }
                    _tileBuilder->addMultiLineString(layerIndex, std::move(lines), id, std::move(properties), false);
                }
                else if (auto multiPolygon = std::dynamic_pointer_cast<MultiPolygonGeometry>(geometry))
                {
                    mbvtbuilder::MBVTTileBuilder::MultiPolygon polygons;
                    polygons.reserve(multiPolygon->getGeometryCount());
                    for (int i = 0; i < multiPolygon->getGeometryCount(); i++)
                    {
                        polygons.push_back(convertPointsList(projection, multiPolygon->getGeometry(i)->getRings()));
                    }
                    _tileBuilder->addMultiPolygon(layerIndex, std::move(polygons), id, std::move(properties), false);
                }
                else
                {
                    throw InvalidArgumentException("Unsupported geometry type in feature collection");
                }
            }
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::setLayerGeoJSON: Failed to update layer: %s", ex.what());
            throw GenericException("Failed to set layer contents", ex.what());
        }
        notifyTilesChanged(false);
    }

    void GeoJSONVectorTileDataSource::deleteLayer(int layerIndex)
    {
        try
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _tileBuilder->deleteLayer(layerIndex);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::deleteLayer: Failed to delete layer: %s", ex.what());
        }
        notifyTilesChanged(false);
    }

    MapBounds GeoJSONVectorTileDataSource::getDataExtent() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        MapBounds mapBounds;
        for (int layerIndex : _tileBuilder->getLayerIndices())
        {
            // NOTE: layerBounds are flipped
            auto layerBounds = _tileBuilder->getLayerBounds(layerIndex);
            mapBounds.expandToContain(MapPos(layerBounds.min(0), -layerBounds.max(1)));
            mapBounds.expandToContain(MapPos(layerBounds.max(0), -layerBounds.min(1)));
        }
        return mapBounds;
    }

    std::shared_ptr<TileData> GeoJSONVectorTileDataSource::loadTile(const MapTile &mapTile)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Log::Infof("GeoJSONVectorTileDataSource::loadTile: Loading %s", mapTile.toString().c_str());
        try
        {
            protobuf::encoded_message encodedTile;
            _tileBuilder->buildTile(mapTile.getZoom(), mapTile.getX(), mapTile.getY(), encodedTile);
            auto data = std::make_shared<BinaryData>(reinterpret_cast<const unsigned char *>(encodedTile.data().data()), encodedTile.data().size());
            return std::make_shared<TileData>(data);
        }
        catch (const std::exception &ex)
        {
            Log::Errorf("GeoJSONVectorTileDataSource::loadTile: Failed to build tile: %s", ex.what());
            return std::shared_ptr<TileData>();
        }
    }

}
