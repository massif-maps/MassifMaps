#ifndef _POLYGONSTYLE_I
#define _POLYGONSTYLE_I

%module PolygonStyle

!proxy_imports(massif::PolygonStyle, graphics.Bitmap, graphics.Color, styles.LineStyle)

%{
#include "styles/PolygonStyle.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/LineStyle.i"

!polymorphic_shared_ptr(massif::PolygonStyle, styles.PolygonStyle)

!spec(massif::PolygonStyle, elementstyle, -)

%attributestring(massif::PolygonStyle, std::shared_ptr<massif::LineStyle>, LineStyle, getLineStyle)
%ignore massif::PolygonStyle::getBitmap;
%ignore massif::PolygonStyle::PolygonStyle;

%include "styles/PolygonStyle.h"

#endif
