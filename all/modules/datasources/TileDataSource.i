#ifndef _TILEDATASOURCE_I
#define _TILEDATASOURCE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") TileDataSource

!proxy_imports(massif::TileDataSource, core.MapTile, core.MapBounds, core.StringMap, core.Variant, datasources.components.TileData, projections.Projection)

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
%import "core/Variant.i"
%import "datasources/components/TileData.i"
%import "projections/Projection.i"
%import "datasources/TileDataSource.i"

!polymorphic_shared_ptr(massif::TileDataSource, datasources.TileDataSource)

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
