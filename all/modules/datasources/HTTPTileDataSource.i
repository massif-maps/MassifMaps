#ifndef _HTTPTILEDATASOURCE_I
#define _HTTPTILEDATASOURCE_I

%module(directors="1") HTTPTileDataSource

!proxy_imports(massif::HTTPTileDataSource, core.MapTile, core.MapBounds, core.StringVector, core.StringMap, datasources.TileDataSource, datasources.components.TileData)

%{
#include "datasources/HTTPTileDataSource.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_map.i>
%include <std_vector.i>
%include <massifswig.i>

%import "datasources/TileDataSource.i"
%import "core/StringVector.i"
%import "core/StringMap.i"

!polymorphic_shared_ptr(massif::HTTPTileDataSource, datasources.HTTPTileDataSource)


!spec(massif::HTTPTileDataSource, source, http, alias(url, baseURL), default(minZoom, 0), default(maxZoom, 24))
%attributestring(massif::HTTPTileDataSource, std::string, BaseURL, getBaseURL, setBaseURL)
%attributeval(massif::HTTPTileDataSource, %arg(std::vector<std::string>), Subdomains, getSubdomains, setSubdomains)
%attribute(massif::HTTPTileDataSource, bool, TMSScheme, isTMSScheme, setTMSScheme)
%attribute(massif::HTTPTileDataSource, bool, MaxAgeHeaderCheck, isMaxAgeHeaderCheck, setMaxAgeHeaderCheck)
%attribute(massif::HTTPTileDataSource, int, Timeout, getTimeout, setTimeout)
%attributeval(massif::HTTPTileDataSource, %arg(std::map<std::string, std::string>), HTTPHeaders, getHTTPHeaders, setHTTPHeaders)

%feature("director") massif::HTTPTileDataSource;

%include "datasources/HTTPTileDataSource.h"

#endif
