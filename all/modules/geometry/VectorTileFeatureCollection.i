#ifndef _VECTORTILEFEATURECOLLECTION_I
#define _VECTORTILEFEATURECOLLECTION_I

%module VectorTileFeatureCollection

!proxy_imports(massif::VectorTileFeatureCollection, geometry.FeatureCollection, geometry.VectorTileFeature, geometry.VectorTileFeatureVector)

%{
#include "geometry/VectorTileFeatureCollection.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "geometry/FeatureCollection.i"
%import "geometry/VectorTileFeature.i"

!polymorphic_shared_ptr(massif::VectorTileFeatureCollection, geometry.VectorTileFeatureCollection)

!method(massif::VectorTileFeatureCollection, getFeature, arg(index, int), returns(object, massif::VectorTileFeature))
%std_exceptions(massif::VectorTileFeatureCollection::getFeature)

%include "geometry/VectorTileFeatureCollection.h"

#endif
