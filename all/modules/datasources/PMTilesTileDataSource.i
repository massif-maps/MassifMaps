#ifndef _PMTILESTILEDATASOURCE_I
#define _PMTILESTILEDATASOURCE_I

%module(directors="1") PMTilesTileDataSource

#ifdef _MASSIF_OFFLINE_SUPPORT

!proxy_imports(massif::PMTilesTileDataSource, core.MapTile, core.MapBounds, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/PMTilesTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/MapTile.i"
%import "datasources/TileDataSource.i"
%import "datasources/components/TileData.i"

!polymorphic_shared_ptr(massif::PMTilesTileDataSource, datasources.PMTilesTileDataSource)

!spec(massif::PMTilesTileDataSource, source, pmtiles, default(minZoom, 0), default(maxZoom, 24))
%std_io_exceptions(massif::PMTilesTileDataSource::PMTilesTileDataSource)

%feature("director") massif::PMTilesTileDataSource;

%include "datasources/PMTilesTileDataSource.h"

#endif

#endif
