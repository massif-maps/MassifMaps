#ifndef _GDALRASTERTILEDATASOURCE_I
#define _GDALRASTERTILEDATASOURCE_I

%module(directors="1") GDALRasterTileDataSource

#ifdef _MASSIF_GDAL_SUPPORT

!proxy_imports(massif::GDALRasterTileDataSource, core.MapTile, core.MapBounds, datasources.components.TileData)

%{
#include "datasources/GDALRasterTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapTile.i"
%import "core/MapBounds.i"
%import "datasources/TileDataSource.i"
%import "datasources/components/TileData.i"

!polymorphic_shared_ptr(massif::GDALRasterTileDataSource, datasources.GDALRasterTileDataSource)

// Two constructors: the four-argument one takes an explicit SRS, the three-argument one reads it
// from the file. A spec with no 'srs' key resolves to the latter.
!spec(massif::GDALRasterTileDataSource, source, gdal, alias(path, fileName), default(minZoom, 0), default(maxZoom, 24))

%std_io_exceptions(massif::GDALRasterTileDataSource::GDALRasterTileDataSource)

%feature("director") massif::GDALRasterTileDataSource;

%include "datasources/GDALRasterTileDataSource.h"

#endif

#endif
