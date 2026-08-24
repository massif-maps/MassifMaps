#ifndef _OGRVECTORDATASOURCE_I
#define _OGRVECTORDATASOURCE_I

%module(directors="1") OGRVectorDataSource

#ifdef _MASSIF_GDAL_SUPPORT

!proxy_imports(massif::OGRVectorDataSource, core.MapBounds, core.StringVector, datasources.VectorDataSource, datasources.components.VectorData, datasources.OGRVectorDataBase, datasources.OGRFieldType, datasources.OGRGeometryType, geometry.GeometrySimplifier, projections.Projection, renderers.components.CullState, styles.StyleSelector, vectorelements.VectorElement, vectorelements.VectorElementVector)

%{
#include "datasources/OGRVectorDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/MapBounds.i"
%import "core/StringVector.i"
%import "datasources/VectorDataSource.i"
%import "datasources/OGRVectorDataBase.i"
%import "geometry/GeometrySimplifier.i"
%import "styles/StyleSelector.i"
%import "vectorelements/VectorElement.i"

!polymorphic_shared_ptr(massif::OGRVectorDataSource, datasources.OGRVectorDataSource)

// Two constructors: 'path' opens the file itself, 'database' + 'layerIndex' takes one layer of an
// OGRVectorDataBase already open. Both take the style selector that picks a style per feature.
!spec(massif::OGRVectorDataSource, source, ogr, alias(path, fileName), alias(style, styleSelector), alias(database, dataBase), default(layerIndex, 0))

%feature("director") massif::OGRVectorDataSource;

%attribute(massif::OGRVectorDataSource, int, FeatureCount, getFeatureCount)
%attribute(massif::OGRVectorDataSource, OGRGeometryType::OGRGeometryType, GeometryType, getGeometryType)
%attributeval(massif::OGRVectorDataSource, std::vector<std::string>, FieldNames, getFieldNames)
%attributestring(massif::OGRVectorDataSource, std::string, CodePage, getCodePage, setCodePage)
!attributestring_polymorphic(massif::OGRVectorDataSource, geometry.GeometrySimplifier, GeometrySimplifier, getGeometrySimplifier, setGeometrySimplifier)
%std_exceptions(massif::OGRVectorDataSource::OGRVectorDataSource)
%std_exceptions(massif::OGRVectorDataSource::add)
%std_exceptions(massif::OGRVectorDataSource::remove)

%include "datasources/OGRVectorDataSource.h"

#endif

#endif
