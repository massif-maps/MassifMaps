#ifndef _CELESTIALOBJECT_I
#define _CELESTIALOBJECT_I

#pragma SWIG nowarn=401

%module CelestialObject

!proxy_imports(massif::CelestialObject, core.MapPos, core.Variant, graphics.Color)

%{
#include "celestial/CelestialObject.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_string.i>
%include <std_shared_ptr.i>
%include <std_vector.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "core/Variant.i"
%import "graphics/Color.i"

!polymorphic_shared_ptr(massif::CelestialObject, celestial.CelestialObject)
!method(massif::CelestialObject, setDirection, arg(azimuth, float), arg(altitude, float), arg(distance, float), returns(void))
!value_type(std::vector<std::shared_ptr<massif::CelestialObject> >, celestial.CelestialObjectVector)

%attribute(massif::CelestialObject, bool, DirectionAnchored, isDirectionAnchored)
%attribute(massif::CelestialObject, float, Azimuth, getAzimuth)
%attribute(massif::CelestialObject, float, Altitude, getAltitude)
%attribute(massif::CelestialObject, double, Distance, getDistance)
%attributeval(massif::CelestialObject, massif::MapPos, Position, getPosition)
%attribute(massif::CelestialObject, double, PositionAltitude, getPositionAltitude)
%attributeval(massif::CelestialObject, massif::Color, Color, getColor, setColor)
%attribute(massif::CelestialObject, bool, Visible, isVisible, setVisible)
%ignore massif::CelestialObject::calculateDirectionVector;
%ignore massif::CelestialObject::setComponents;
%ignore massif::CelestialObject::getLayer;
%std_exceptions(massif::CelestialObject::CelestialObject)
!standard_equals(massif::CelestialObject);

%include "celestial/CelestialObject.h"

!value_template(std::vector<std::shared_ptr<massif::CelestialObject> >, celestial.CelestialObjectVector);

#endif
