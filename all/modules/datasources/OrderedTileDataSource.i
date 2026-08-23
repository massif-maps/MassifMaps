#ifndef _ORDEREDTILEDATASOURCE_I
#define _ORDEREDTILEDATASOURCE_I

%module(directors="1") OrderedTileDataSource

!proxy_imports(massif::OrderedTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/OrderedTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"

!polymorphic_shared_ptr(massif::OrderedTileDataSource, datasources.OrderedTileDataSource)


!spec(massif::OrderedTileDataSource, source, ordered, alias(source, dataSource1), alias(source2, dataSource2))
%std_exceptions(massif::OrderedTileDataSource::OrderedTileDataSource)

%feature("director") massif::OrderedTileDataSource;

%include "datasources/OrderedTileDataSource.h"

#endif
