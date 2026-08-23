#ifndef _ASSETTILEDATASOURCE_I
#define _ASSETTILEDATASOURCE_I

%module(directors="1") AssetTileDataSource

!proxy_imports(massif::AssetTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/AssetTileDataSource.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"

!polymorphic_shared_ptr(massif::AssetTileDataSource, datasources.AssetTileDataSource)


!spec(massif::AssetTileDataSource, source, assets, alias(path, basePath), default(minZoom, 0), default(maxZoom, 24))
%feature("director") massif::AssetTileDataSource;

%include "datasources/AssetTileDataSource.h"

#endif
