#ifndef _FEATURECOLLECTION_I
#define _FEATURECOLLECTION_I

%module FeatureCollection

!proxy_imports(massif::FeatureCollection, geometry.Feature, geometry.FeatureVector)

%{
#include "geometry/FeatureCollection.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "geometry/Feature.i"

!polymorphic_shared_ptr(massif::FeatureCollection, geometry.FeatureCollection)

!method(massif::FeatureCollection, getFeature, arg(index, int), returns(object, massif::Feature))
// The whole collection as GeoJSON in WGS84; pass the projection its coordinates are in (a tile source's), or none for WGS84.
!method(massif::FeatureCollection, toGeoJSON, arg(sourceProjection, handle), returns(json))
%attribute(massif::FeatureCollection, int, FeatureCount, getFeatureCount)
%std_exceptions(massif::FeatureCollection::getFeature)

%include "geometry/FeatureCollection.h"

#endif
