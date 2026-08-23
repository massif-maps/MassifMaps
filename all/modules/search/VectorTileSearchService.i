#ifndef _VECTORTILESEARCHSERVICE_I
#define _VECTORTILESEARCHSERVICE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") VectorTileSearchService

#ifdef _MASSIF_SEARCH_SUPPORT

!proxy_imports(massif::VectorTileSearchService, core.StringVector, search.SearchRequest, datasources.TileDataSource, geometry.VectorTileFeatureCollection, vectortiles.VectorTileDecoder, projections.Projection)

%{
#include "search/VectorTileSearchService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>
%include <std_vector.i>

%import "geometry/VectorTileFeatureCollection.i"
%import "search/SearchRequest.i"
%import "datasources/TileDataSource.i"
%import "vectortiles/VectorTileDecoder.i"
%import "projections/Projection.i"
%import "core/StringVector.i"

!polymorphic_shared_ptr(massif::VectorTileSearchService, search.VectorTileSearchService)


!method(massif::VectorTileSearchService, findFeatures, arg(request, handle), returns(object, massif::VectorTileFeatureCollection))
!spec(massif::VectorTileSearchService, search, vectortile, alias(source, dataSource), alias(style, tileDecoder))
%attributestring(massif::VectorTileSearchService, std::shared_ptr<massif::TileDataSource>, DataSource, getDataSource)
%attributestring(massif::VectorTileSearchService, std::shared_ptr<massif::VectorTileDecoder>, TileDecoder, getTileDecoder)
!attributestring_polymorphic(massif::VectorTileSearchService, projections.Projection, Projection, getProjection)
%attribute(massif::VectorTileSearchService, int, MinZoom, getMinZoom, setMinZoom)
%attribute(massif::VectorTileSearchService, int, MaxZoom, getMaxZoom, setMaxZoom)
%attribute(massif::VectorTileSearchService, int, MaxResults, getMaxResults, setMaxResults)
%attribute(massif::VectorTileSearchService, bool, SortByDistance, getSortByDistance, setSortByDistance)
%attribute(massif::VectorTileSearchService, bool, PreventDuplicates, getPreventDuplicates, setPreventDuplicates)
%attributeval(massif::VectorTileSearchService, %arg(std::vector<std::string>), Layers, getLayers, setLayers)
%std_exceptions(massif::VectorTileSearchService::VectorTileSearchService)
%std_exceptions(massif::VectorTileSearchService::findFeatures)

%feature("director") massif::VectorTileSearchService;

%include "search/VectorTileSearchService.h"

#endif

#endif
