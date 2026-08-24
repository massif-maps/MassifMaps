#ifndef _PERSISTENTCACHETILEDATASOURCE_I
#define _PERSISTENTCACHETILEDATASOURCE_I

%module(directors="1") PersistentCacheTileDataSource

#ifdef _MASSIF_OFFLINE_SUPPORT

!proxy_imports(massif::PersistentCacheTileDataSource, core.MapBounds, core.MapTile, core.MapBounds, core.StringMap, datasources.CacheTileDataSource, datasources.TileDownloadListener, datasources.TileDownloadInfo, datasources.components.TileData)

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
%import "datasources/TileDownloadInfo.i"

!polymorphic_shared_ptr(massif::PersistentCacheTileDataSource, datasources.PersistentCacheTileDataSource)


!spec(massif::PersistentCacheTileDataSource, source, persistent-cache, alias(source, dataSource))
// Downloading an area offline: a method to start it, events to follow it. The SDK reports progress
// through a listener passed to the call, so the facade installs its own and emits on this source -
// which is what makes an offline download reachable from a binding at all.
!method(massif::PersistentCacheTileDataSource, startDownloadArea, arg(bounds, json), arg(minZoom, int), arg(maxZoom, int), arg(fetchDelay, int), returns(void))
!method(massif::PersistentCacheTileDataSource, stopAllDownloads, returns(void))
!method(massif::PersistentCacheTileDataSource, clear, returns(void))
!event(massif::PersistentCacheTileDataSource, download.started, payload(massif::TileDownloadInfo))
!event(massif::PersistentCacheTileDataSource, download.progress, payload(massif::TileDownloadInfo))
!event(massif::PersistentCacheTileDataSource, download.failed, payload(massif::TileDownloadInfo))
!event(massif::PersistentCacheTileDataSource, download.completed)
%attribute(massif::PersistentCacheTileDataSource, bool, CacheOnlyMode, isCacheOnlyMode, setCacheOnlyMode)
%attribute(massif::PersistentCacheTileDataSource, bool, Open, isOpen)
%std_exceptions(massif::PersistentCacheTileDataSource::PersistentCacheTileDataSource)
%std_exceptions(massif::PersistentCacheTileDataSource::startDownloadArea)

%feature("director") massif::PersistentCacheTileDataSource;

%include "datasources/PersistentCacheTileDataSource.h"

#endif

#endif
