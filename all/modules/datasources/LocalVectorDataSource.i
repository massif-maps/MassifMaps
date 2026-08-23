#ifndef _LOCALVECTORDATASOURCE_I
#define _LOCALVECTORDATASOURCE_I

%module(directors="1") LocalVectorDataSource

!proxy_imports(massif::LocalVectorDataSource, core.MapBounds, datasources.VectorDataSource, datasources.components.VectorData, geometry.FeatureCollection, geometry.GeometrySimplifier, projections.Projection, renderers.components.CullState, styles.Style, vectorelements.VectorElement, vectorelements.VectorElementVector)

%{
#include "datasources/LocalVectorDataSource.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/VectorDataSource.i"
%import "geometry/GeometrySimplifier.i"
%import "geometry/FeatureCollection.i"
%import "styles/Style.i"
%import "vectorelements/VectorElement.i"

!enum(massif::LocalSpatialIndexType::LocalSpatialIndexType)
!polymorphic_shared_ptr(massif::LocalVectorDataSource, datasources.LocalVectorDataSource)


!method(massif::LocalVectorDataSource, add, arg(element, handle), returns(void))
!method(massif::LocalVectorDataSource, remove, arg(element, handle), returns(bool))
!method(massif::LocalVectorDataSource, clear, returns(void))
!spec(massif::LocalVectorDataSource, source, local)
%feature("director") massif::LocalVectorDataSource;

!attributestring_polymorphic(massif::LocalVectorDataSource, geometry.GeometrySimplifier, GeometrySimplifier, getGeometrySimplifier, setGeometrySimplifier)
%std_exceptions(massif::LocalVectorDataSource::LocalVectorDataSource)
%std_exceptions(massif::LocalVectorDataSource::setAll)
%std_exceptions(massif::LocalVectorDataSource::add)
%std_exceptions(massif::LocalVectorDataSource::addAll)
%std_exceptions(massif::LocalVectorDataSource::remove)
%std_exceptions(massif::LocalVectorDataSource::removeAll)
%std_exceptions(massif::LocalVectorDataSource::addFeatureCollection)

%include "datasources/LocalVectorDataSource.h"

#endif
