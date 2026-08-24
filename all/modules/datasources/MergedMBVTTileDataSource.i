#ifndef _MERGEDMBVTTILEDATASOURCE_I
#define _MERGEDMBVTTILEDATASOURCE_I

%module(directors="1") MergedMBVTTileDataSource

!proxy_imports(massif::MergedMBVTTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/MergedMBVTTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"

!polymorphic_shared_ptr(massif::MergedMBVTTileDataSource, datasources.MergedMBVTTileDataSource)

!spec(massif::MergedMBVTTileDataSource, source, merged-mbvt, alias(source, dataSource1), alias(source2, dataSource2))

%std_exceptions(massif::MergedMBVTTileDataSource::MergedMBVTTileDataSource)

%feature("director") massif::MergedMBVTTileDataSource;

%include "datasources/MergedMBVTTileDataSource.h"

#endif
