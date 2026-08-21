#ifndef _FEATURE_I
#define _FEATURE_I

%module Feature

!proxy_imports(massif::Feature, core.Variant, geometry.Geometry)

%{
#include "geometry/Feature.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/Variant.i"
%import "geometry/Geometry.i"

!polymorphic_shared_ptr(massif::Feature, geometry.Feature)
!value_type(std::vector<std::shared_ptr<massif::Feature> >, geometry.FeatureVector)

!attributestring_polymorphic(massif::Feature, geometry.Geometry, Geometry, getGeometry)
%attributeval(massif::Feature, massif::Variant, Properties, getProperties)
%attributestring(massif::Feature, std::string, GeometryGeoJSON, getGeometryGeoJSON)
!standard_equals(massif::Feature);

%include "geometry/Feature.h"

!value_template(std::vector<std::shared_ptr<massif::Feature> >, geometry.FeatureVector)

#endif
