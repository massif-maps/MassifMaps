#ifndef _CELESTIALIMAGE_I
#define _CELESTIALIMAGE_I

%module(directors="1") CelestialImage

!proxy_imports(massif::CelestialImage, celestial.CelestialObject, core.DoubleVector, graphics.Bitmap)

%{
#include "celestial/CelestialImage.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_vector.i>
%include <massifswig.i>

%import "celestial/CelestialObject.i"
%import "core/DoubleVector.i"
%import "graphics/Bitmap.i"

!polymorphic_shared_ptr(massif::CelestialImage, celestial.CelestialImage)
!spec(massif::CelestialImage, celestial, image)
// [u, v, azimuth, altitude] per anchor, three anchors, flat, as the C++ takes them.
!method(massif::CelestialImage, setAnchors, arg(anchors, json), returns(void))

!attributestring_polymorphic(massif::CelestialImage, graphics.Bitmap, Bitmap, getBitmap, setBitmap)
%attribute(massif::CelestialImage, bool, LuminanceAlpha, isLuminanceAlpha, setLuminanceAlpha)
%ignore massif::CelestialImage::buildDirections;
%std_exceptions(massif::CelestialImage::CelestialImage)
%std_exceptions(massif::CelestialImage::setAnchors)

%include "celestial/CelestialImage.h"

#endif
