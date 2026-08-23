#ifndef _COMBINEDTILEDATASOURCE_I
#define _COMBINEDTILEDATASOURCE_I

%module(directors="1") CombinedTileDataSource

!proxy_imports(massif::CombinedTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/CombinedTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"

!polymorphic_shared_ptr(massif::CombinedTileDataSource, datasources.CombinedTileDataSource)


!spec(massif::CombinedTileDataSource, source, combined, alias(source, dataSource1), alias(source2, dataSource2), default(zoomLevel, 0))
%std_exceptions(massif::CombinedTileDataSource::CombinedTileDataSource)

%feature("director") massif::CombinedTileDataSource;

%include "datasources/CombinedTileDataSource.h"

#endif
