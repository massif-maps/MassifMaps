#ifndef _POINTDETAILTILEDATASOURCE_I
#define _POINTDETAILTILEDATASOURCE_I

%module(directors="1") PointDetailTileDataSource

!proxy_imports(massif::PointDetailTileDataSource, core.MapTile, core.MapBounds, core.StringMap, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/PointDetailTileDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"

!polymorphic_shared_ptr(massif::PointDetailTileDataSource, datasources.PointDetailTileDataSource)

!spec(massif::PointDetailTileDataSource, source, point-detail, alias(source, dataSource), alias(layer, layerName), default(detailZoom, 14))

%attributestring(massif::PointDetailTileDataSource, std::string, LayerName, getLayerName)
%attribute(massif::PointDetailTileDataSource, int, DetailZoom, getDetailZoom, setDetailZoom)
%attribute(massif::PointDetailTileDataSource, int, MaxDetailLevels, getMaxDetailLevels, setMaxDetailLevels)
%attribute(massif::PointDetailTileDataSource, int, MaxFeatures, getMaxFeatures, setMaxFeatures)
%attributestring(massif::PointDetailTileDataSource, std::string, RankProperty, getRankProperty, setRankProperty)

%std_exceptions(massif::PointDetailTileDataSource::PointDetailTileDataSource)

%feature("director") massif::PointDetailTileDataSource;

%include "datasources/PointDetailTileDataSource.h"

#endif
