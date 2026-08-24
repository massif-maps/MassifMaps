#ifndef _POINTSTYLE_I
#define _POINTSTYLE_I

%module PointStyle

!proxy_imports(massif::PointStyle, graphics.Bitmap, graphics.Color, styles.Style)

%{
#include "styles/PointStyle.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "graphics/Bitmap.i"
%import "styles/Style.i"

!polymorphic_shared_ptr(massif::PointStyle, styles.PointStyle)

!spec(massif::PointStyle, elementstyle, -)

%attribute(massif::PointStyle, float, Size, getSize)
%attribute(massif::PointStyle, float, ClickSize, getClickSize)
%attributestring(massif::PointStyle, std::shared_ptr<massif::Bitmap>, Bitmap, getBitmap)
%ignore massif::PointStyle::PointStyle;

%include "styles/PointStyle.h"

#endif
