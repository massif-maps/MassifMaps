#ifndef _CELESTIALSPRITE_I
#define _CELESTIALSPRITE_I

%module(directors="1") CelestialSprite

!proxy_imports(massif::CelestialSprite, celestial.CelestialObject, graphics.Bitmap)

%{
#include "celestial/CelestialSprite.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "celestial/CelestialObject.i"
%import "graphics/Bitmap.i"

!polymorphic_shared_ptr(massif::CelestialSprite, celestial.CelestialSprite)
!spec(massif::CelestialSprite, celestial, sprite)

%attribute(massif::CelestialSprite, float, AngularSize, getAngularSize, setAngularSize)
%attribute(massif::CelestialSprite, float, ScreenSize, getScreenSize, setScreenSize)
!attributestring_polymorphic(massif::CelestialSprite, graphics.Bitmap, Bitmap, getBitmap, setBitmap)
%attribute(massif::CelestialSprite, float, Softness, getSoftness, setSoftness)
%attribute(massif::CelestialSprite, float, ClickRadius, getClickRadius, setClickRadius)
%std_exceptions(massif::CelestialSprite::CelestialSprite)

%include "celestial/CelestialSprite.h"

#endif
