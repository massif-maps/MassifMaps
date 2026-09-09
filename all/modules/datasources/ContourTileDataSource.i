#ifndef _CONTOURTILEDATASOURCE_I
#define _CONTOURTILEDATASOURCE_I

%module(directors="1") ContourTileDataSource

!proxy_imports(massif::ContourTileDataSource, core.MapTile, core.MapBounds, datasources.TileDataSource, datasources.components.TileData, rastertiles.ElevationDecoder, components.TerrainOptions)

%{
#include "datasources/ContourTileDataSource.h"
#include "components/TerrainOptions.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"
%import "datasources/components/TileData.i"
%import "rastertiles/ElevationDecoder.i"
%import "components/TerrainOptions.i"

!polymorphic_shared_ptr(massif::ContourTileDataSource, datasources.ContourTileDataSource)

// `source` rather than `dataSource`, to read like every other wrapping source spec (merged-mbvt,
// ordered, persistent-cache). The elevation decoder is deliberately NOT a spec key: it is resolved
// from the wrapped source's `dem_encoding` metadata, the same place a hillshade layer reads it.
!spec(massif::ContourTileDataSource, source, contour, alias(source, dataSource))

%attributestring(massif::ContourTileDataSource, std::string, LayerName, getLayerName, setLayerName)
%attribute(massif::ContourTileDataSource, float, BaseInterval, getBaseInterval, setBaseInterval)
%attribute(massif::ContourTileDataSource, int, Resolution, getResolution, setResolution)
%attribute(massif::ContourTileDataSource, int, MinVisibleZoom, getMinVisibleZoom, setMinVisibleZoom)
%attribute(massif::ContourTileDataSource, bool, SeamlessEdgesEnabled, isSeamlessEdgesEnabled, setSeamlessEdgesEnabled)
%attributestring(massif::ContourTileDataSource, std::shared_ptr<massif::TerrainOptions>, TerrainOptions, getTerrainOptions, setTerrainOptions)
%attribute(massif::ContourTileDataSource, bool, LabelStubsEnabled, isLabelStubsEnabled, setLabelStubsEnabled)
%attribute(massif::ContourTileDataSource, float, LabelInterval, getLabelInterval, setLabelInterval)
%attribute(massif::ContourTileDataSource, float, SimplifyTolerance, getSimplifyTolerance, setSimplifyTolerance)

%std_exceptions(massif::ContourTileDataSource::ContourTileDataSource)

%feature("director") massif::ContourTileDataSource;

%include "datasources/ContourTileDataSource.h"

#endif
