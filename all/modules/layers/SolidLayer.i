#ifndef _SOLIDLAYER_I
#define _SOLIDLAYER_I

%module SolidLayer

!proxy_imports(massif::SolidLayer, graphics.Color, graphics.Bitmap, layers.Layer)

%{
#include "layers/SolidLayer.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "layers/Layer.i"
%import "graphics/Color.i"
%import "graphics/Bitmap.i"

!polymorphic_shared_ptr(massif::SolidLayer, layers.SolidLayer)


!spec(massif::SolidLayer, layer, solid)
%attributeval(massif::SolidLayer, massif::Color, Color, getColor, setColor)
%attributestring(massif::SolidLayer, std::shared_ptr<massif::Bitmap>, Bitmap, getBitmap, setBitmap)
%attribute(massif::SolidLayer, float, BitmapScale, getBitmapScale, setBitmapScale)

%include "layers/SolidLayer.h"

#endif
