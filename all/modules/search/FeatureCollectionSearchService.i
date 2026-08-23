#ifndef _FEATURECOLLECTIONSEARCHSERVICE_I
#define _FEATURECOLLECTIONSEARCHSERVICE_I

#pragma SWIG nowarn=325
#pragma SWIG nowarn=401

%module(directors="1") FeatureCollectionSearchService

#ifdef _MASSIF_SEARCH_SUPPORT

!proxy_imports(massif::FeatureCollectionSearchService, search.SearchRequest, geometry.FeatureCollection, projections.Projection)

%{
#include "search/FeatureCollectionSearchService.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "search/SearchRequest.i"
%import "geometry/FeatureCollection.i"
%import "projections/Projection.i"

!polymorphic_shared_ptr(massif::FeatureCollectionSearchService, search.FeatureCollectionSearchService)

!method(massif::FeatureCollectionSearchService, findFeatures, arg(request, handle), returns(object, massif::FeatureCollection))
%attributestring(massif::FeatureCollectionSearchService, std::shared_ptr<massif::Projection>, Projection, getProjection)
%attributestring(massif::FeatureCollectionSearchService, std::shared_ptr<massif::FeatureCollection>, FeatureCollection, getFeatureCollection)
%attribute(massif::FeatureCollectionSearchService, int, MaxResults, getMaxResults, setMaxResults)
%std_exceptions(massif::FeatureCollectionSearchService::FeatureCollectionSearchService)
%std_exceptions(massif::FeatureCollectionSearchService::findFeatures)

%feature("director") massif::FeatureCollectionSearchService;

%include "search/FeatureCollectionSearchService.h"

#endif

#endif
