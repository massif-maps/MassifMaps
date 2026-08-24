#ifndef _TILEDOWNLOADINFO_I
#define _TILEDOWNLOADINFO_I

%module TileDownloadInfo

!proxy_imports(massif::TileDownloadInfo, core.MapTile)

%{
#include "datasources/TileDownloadInfo.h"
#include <memory>
%}

%import <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/MapTile.i"

!shared_ptr(massif::TileDownloadInfo, datasources.TileDownloadInfo)

%attribute(massif::TileDownloadInfo, int, TileCount, getTileCount)
%attribute(massif::TileDownloadInfo, float, Progress, getProgress)
%attributeval(massif::TileDownloadInfo, massif::MapTile, Tile, getTile)
%ignore massif::TileDownloadInfo::TileDownloadInfo;
!standard_equals(massif::TileDownloadInfo);
!custom_tostring(massif::TileDownloadInfo);

%include "datasources/TileDownloadInfo.h"

#endif
