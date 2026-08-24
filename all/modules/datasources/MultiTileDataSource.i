#ifndef _MULTITILEDATASOURCE_I
#define _MULTITILEDATASOURCE_I

%module(directors="1") MultiTileDataSource

!proxy_imports(massif::MultiTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.TileDataSource,  datasources.components.TileData)
%{
#include "datasources/MultiTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/MapTile.i"
%import "core/StringMap.i"
%import "datasources/TileDataSource.i"
#ifdef _MASSIF_OFFLINE_SUPPORT
%import "datasources/MBTilesTileDataSource.i"
%import "datasources/PMTilesTileDataSource.i"
#endif
%import "datasources/components/TileData.i"

!polymorphic_shared_ptr(massif::MultiTileDataSource, datasources.MultiTileDataSource)


// The list is filled after construction - one package per offline area, discovered at run time -
// so add/remove are the spec, not a constructor argument.
!spec(massif::MultiTileDataSource, source, multi, default(maxOpenedPackages, 4))
!method(massif::MultiTileDataSource, add, arg(datasource, handle), arg(tileMask, string), returns(void))
!method(massif::MultiTileDataSource, remove, arg(datasource, handle), returns(bool))
%std_exceptions(massif::MultiTileDataSource::MultiTileDataSource)
%std_exceptions(massif::LocalVectorDataSource::add)
%std_exceptions(massif::LocalVectorDataSource::remove)

%feature("director") massif::MultiTileDataSource;

%include "datasources/MultiTileDataSource.h"

#endif
