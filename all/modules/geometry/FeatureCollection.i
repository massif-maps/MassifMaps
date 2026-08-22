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
%attribute(massif::FeatureCollection, int, FeatureCount, getFeatureCount)
%std_exceptions(massif::FeatureCollection::getFeature)

%include "geometry/FeatureCollection.h"

#endif
