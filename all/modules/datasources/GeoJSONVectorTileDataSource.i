#ifndef _GEOJSONVECTORTILEDATASOURCE_I
#define _GEOJSONVECTORTILEDATASOURCE_I

%module(directors="1") GeoJSONVectorTileDataSource

!proxy_imports(massif::GeoJSONVectorTileDataSource, core.MapTile, core.MapBounds, core.Variant, datasources.TileDataSource, datasources.components.TileData, geometry.FeatureCollection, projections.Projection)

%{
#include "datasources/GeoJSONVectorTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/MapTile.i"
%import "core/Variant.i"
%import "geometry/FeatureCollection.i"
%import "datasources/TileDataSource.i"
%import "datasources/components/TileData.i"
%import "projections/Projection.i"

!polymorphic_shared_ptr(massif::GeoJSONVectorTileDataSource, datasources.GeoJSONVectorTileDataSource)

!method(massif::GeoJSONVectorTileDataSource, createLayer, arg(name, string), returns(int))
!method(massif::GeoJSONVectorTileDataSource, setLayerGeoJSON, arg(layer, int), arg(geoJson, json), returns(void))
!method(massif::GeoJSONVectorTileDataSource, deleteLayer, arg(layer, int), returns(void))
// One feature at a time, so an app editing a saved item does not re-encode the whole document.
!method(massif::GeoJSONVectorTileDataSource, addFeature, arg(layer, int), arg(feature, json), returns(void))
!method(massif::GeoJSONVectorTileDataSource, updateFeature, arg(layer, int), arg(feature, json), returns(void))
!method(massif::GeoJSONVectorTileDataSource, removeFeature, arg(layer, int), arg(id, json), returns(void))
// A GeoJSON document served AS vector tiles, which is how "add a GeoJSON line" is done without a
// separate render path: the features go through the same style and the same renderer as a tile
// source's. Build it, createLayer(name), then setLayerGeoJSON(index, document).
!spec(massif::GeoJSONVectorTileDataSource, source, geojson, default(minZoom, 0), default(maxZoom, 14))

%attribute(massif::GeoJSONVectorTileDataSource, float, SimplifyTolerance, getSimplifyTolerance, setSimplifyTolerance)
%attribute(massif::GeoJSONVectorTileDataSource, float, DefaultLayerBuffer, getDefaultLayerBuffer, setDefaultLayerBuffer)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::createLayer)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::setLayerGeoJSON)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::setLayerGeoJSONString)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::addGeoJSONFeature)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::updateGeoJSONFeature)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::addGeoJSONStringFeature)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::updateGeoJSONStringFeature)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::removeGeoJSONFeature)
%std_io_exceptions(massif::GeoJSONVectorTileDataSource::setLayerFeatureCollection)

%feature("director") massif::GeoJSONVectorTileDataSource;

%include "datasources/GeoJSONVectorTileDataSource.h"

#endif
