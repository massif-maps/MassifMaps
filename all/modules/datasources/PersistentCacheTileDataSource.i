#ifndef _PERSISTENTCACHETILEDATASOURCE_I
#define _PERSISTENTCACHETILEDATASOURCE_I

%module(directors="1") PersistentCacheTileDataSource

#ifdef _MASSIF_OFFLINE_SUPPORT

!proxy_imports(massif::PersistentCacheTileDataSource, core.MapBounds, core.MapTile, core.MapBounds, core.StringMap, datasources.CacheTileDataSource, datasources.TileDownloadListener, datasources.components.TileData)

%{
#include "datasources/PersistentCacheTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "datasources/CacheTileDataSource.i"
%import "datasources/TileDownloadListener.i"

!polymorphic_shared_ptr(massif::PersistentCacheTileDataSource, datasources.PersistentCacheTileDataSource)


!spec(massif::PersistentCacheTileDataSource, source, persistent-cache, alias(source, dataSource))
%attribute(massif::PersistentCacheTileDataSource, bool, CacheOnlyMode, isCacheOnlyMode, setCacheOnlyMode)
%attribute(massif::PersistentCacheTileDataSource, bool, Open, isOpen)
%std_exceptions(massif::PersistentCacheTileDataSource::PersistentCacheTileDataSource)
%std_exceptions(massif::PersistentCacheTileDataSource::startDownloadArea)

%feature("director") massif::PersistentCacheTileDataSource;

%include "datasources/PersistentCacheTileDataSource.h"

#endif

#endif
