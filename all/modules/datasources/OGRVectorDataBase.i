#ifndef _OGRVECTORDATABASE_I
#define _OGRVECTORDATABASE_I

%module OGRVectorDataBase

#ifdef _MASSIF_GDAL_SUPPORT

!proxy_imports(massif::OGRVectorDataBase, datasources.OGRGeometryType, core.StringVector)

%{
#include "datasources/OGRVectorDataBase.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/StringVector.i"

!polymorphic_shared_ptr(massif::OGRVectorDataBase, datasources.OGRVectorDataBase)

// Not a data source - the file an OGRVectorDataSource reads one layer of.
!spec(massif::OGRVectorDataBase, data, ogr-database, alias(path, fileName), default(writable, false))

%attribute(massif::OGRVectorDataBase, int, LayerCount, getLayerCount)
%attributeval(massif::OGRVectorDataBase, std::vector<std::string>, LayerNames, getLayerNames)
%std_io_exceptions(massif::OGRVectorDataBase::OGRVectorDataBase)

%include "datasources/OGRVectorDataBase.h"

#endif

#endif
