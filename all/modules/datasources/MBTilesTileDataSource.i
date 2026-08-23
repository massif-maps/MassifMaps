#ifndef _MBTILESTILEDATASOURCE_I
#define _MBTILESTILEDATASOURCE_I

%module(directors="1") MBTilesTileDataSource

#ifdef _MASSIF_OFFLINE_SUPPORT

!proxy_imports(massif::MBTilesTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/MBTilesTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/MapTile.i"
%import "core/StringMap.i"
%import "datasources/TileDataSource.i"
%import "datasources/components/TileData.i"

!enum(massif::MBTilesScheme::MBTilesScheme)
!polymorphic_shared_ptr(massif::MBTilesTileDataSource, datasources.MBTilesTileDataSource)


!spec(massif::MBTilesTileDataSource, source, mbtiles, default(minZoom, 0), default(maxZoom, 24))
%std_io_exceptions(massif::MBTilesTileDataSource::MBTilesTileDataSource)

%feature("director") massif::MBTilesTileDataSource;

%include "datasources/MBTilesTileDataSource.h"

#endif

#endif
