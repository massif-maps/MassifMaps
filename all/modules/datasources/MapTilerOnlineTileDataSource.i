#ifndef _MAPTILERONLINETILEDATASOURCE_I
#define _MAPTILERONLINETILEDATASOURCE_I

%module(directors="1") MapTilerOnlineTileDataSource

!proxy_imports(massif::MapTilerOnlineTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/MapTilerOnlineTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"

!polymorphic_shared_ptr(massif::MapTilerOnlineTileDataSource, datasources.MapTilerOnlineTileDataSource)

!spec(massif::MapTilerOnlineTileDataSource, source, maptiler)

%attributestring(massif::MapTilerOnlineTileDataSource, std::string, CustomServiceURL, getCustomServiceURL, setCustomServiceURL)
%attribute(massif::MapTilerOnlineTileDataSource, int, Timeout, getTimeout, setTimeout)
%std_exceptions(massif::MapTilerOnlineTileDataSource::MapTilerOnlineTileDataSource)
%ignore massif::MapTilerOnlineTileDataSource::buildTileURL;
%ignore massif::MapTilerOnlineTileDataSource::loadConfiguration;
%ignore massif::MapTilerOnlineTileDataSource::loadOnlineTile;

%feature("director") massif::MapTilerOnlineTileDataSource;

%include "datasources/MapTilerOnlineTileDataSource.h"

#endif
