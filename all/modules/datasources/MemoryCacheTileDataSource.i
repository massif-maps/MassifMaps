#ifndef _MEMORYCACHETILEDATASOURCE_I
#define _MEMORYCACHETILEDATASOURCE_I

%module(directors="1") MemoryCacheTileDataSource

!proxy_imports(massif::MemoryCacheTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.CacheTileDataSource, datasources.components.TileData)

%{
#include "datasources/MemoryCacheTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "datasources/CacheTileDataSource.i"

!polymorphic_shared_ptr(massif::MemoryCacheTileDataSource, datasources.MemoryCacheTileDataSource)


!spec(massif::MemoryCacheTileDataSource, source, memory-cache, alias(source, dataSource))
%std_exceptions(massif::MemoryCacheTileDataSource::MemoryCacheTileDataSource)

%feature("director") massif::MemoryCacheTileDataSource;

%include "datasources/MemoryCacheTileDataSource.h"

#endif
