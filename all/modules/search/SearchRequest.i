#ifndef _SEARCHREQUEST_I
#define _SEARCHREQUEST_I

#pragma SWIG nowarn=325

%module SearchRequest

#ifdef _MASSIF_SEARCH_SUPPORT

!proxy_imports(massif::SearchRequest, geometry.Geometry, projections.Projection)

%{
#include "search/SearchRequest.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <massifswig.i>

%import "geometry/Geometry.i"
%import "projections/Projection.i"

!shared_ptr(massif::SearchRequest, search.SearchRequest)


!spec(massif::SearchRequest, search, request)
%attributestring(massif::SearchRequest, std::string, FilterExpression, getFilterExpression, setFilterExpression)
%attributestring(massif::SearchRequest, std::string, RegexFilter, getRegexFilter, setRegexFilter)
%attributestring(massif::SearchRequest, std::shared_ptr<massif::Geometry>, Geometry, getGeometry, setGeometry)
%attributestring(massif::SearchRequest, std::shared_ptr<massif::Projection>, Projection, getProjection, setProjection)
%attribute(massif::SearchRequest, float, SearchRadius, getSearchRadius, setSearchRadius)
!standard_equals(massif::SearchRequest);
!custom_tostring(massif::SearchRequest);

%include "search/SearchRequest.h"

#endif

#endif
