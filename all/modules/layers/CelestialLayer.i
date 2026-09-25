#ifndef _CELESTIALLAYER_I
#define _CELESTIALLAYER_I

%module CelestialLayer

!proxy_imports(massif::CelestialLayer, celestial.CelestialObject, celestial.CelestialObjectVector, layers.Layer, layers.CelestialEventListener)

%{
#include "layers/CelestialLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_vector.i>
%include <massifswig.i>

%import "layers/Layer.i"
%import "layers/CelestialEventListener.i"
%import "celestial/CelestialObject.i"

!polymorphic_shared_ptr(massif::CelestialLayer, layers.CelestialLayer)
!spec(massif::CelestialLayer, layer, celestial)
!method(massif::CelestialLayer, add, arg(object, handle), returns(void))
!method(massif::CelestialLayer, remove, arg(object, handle), returns(bool))
!method(massif::CelestialLayer, clear, returns(void))

!attributestring_polymorphic(massif::CelestialLayer, layers.CelestialEventListener, CelestialEventListener, getCelestialEventListener, setCelestialEventListener)
%std_exceptions(massif::CelestialLayer::add)
%std_exceptions(massif::CelestialLayer::addAll)
%ignore massif::CelestialLayer::onDrawFrame;
%ignore massif::CelestialLayer::calculateRayIntersectedElements;
%ignore massif::CelestialLayer::processClick;

%include "layers/CelestialLayer.h"

#endif
