#ifndef _TILEDATASOURCE_I
#define _TILEDATASOURCE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") TileDataSource

!proxy_imports(massif::TileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.components.TileData, projections.Projection)

%{
#include "datasources/TileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_map.i>
%include <massifswig.i>

%import "core/MapTile.i"
%import "core/MapBounds.i"
%import "core/StringMap.i"
%import "datasources/components/TileData.i"
%import "projections/Projection.i"
%import "datasources/TileDataSource.i"

!polymorphic_shared_ptr(massif::TileDataSource, datasources.TileDataSource)

// Fetches one tile. Synchronous - an HTTP source does network I/O, so use callAsync.
!method(massif::TileDataSource, loadTile, arg(tile, tile), returns(object, massif::TileData))
// How a DEM tile encodes metres: "terrarium" or "mapbox". The terrain and the hillshade pick
// their elevation decoder from it, so a source says what it holds instead of every consumer
// being handed a decoder.
%attributestring(massif::TileDataSource, std::string, Encoding, getEncoding, setEncoding)
%attribute(massif::TileDataSource, int, MinZoom, getMinZoom)
%attribute(massif::TileDataSource, int, MaxZoom, getMaxZoom)
%attribute(massif::TileDataSource, int, MaxOverzoomLevel, getMaxOverzoomLevel, setMaxOverzoomLevel)
%attributeval(massif::TileDataSource, massif::MapBounds, DataExtent, getDataExtent)
!attributestring_polymorphic(massif::TileDataSource, projections.Projection, Projection, getProjection)
%ignore massif::TileDataSource::OnChangeListener;
%ignore massif::TileDataSource::registerOnChangeListener;
%ignore massif::TileDataSource::unregisterOnChangeListener;
%ignore massif::TileDataSource::buildTileMetadata;

%feature("director") massif::TileDataSource;
%feature("nodirector") massif::TileDataSource::buildTagValues;

%include "datasources/TileDataSource.h"

#endif
