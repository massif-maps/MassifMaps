#ifndef _LINESTYLE_I
#define _LINESTYLE_I

%module LineStyle

!proxy_imports(massif::LineStyle, graphics.Bitmap, graphics.Color, styles.Style)

%{
#include "styles/LineStyle.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "graphics/Bitmap.i"
%import "styles/Style.i"

!enum(massif::LineEndType::LineEndType)
!enum(massif::LineJoinType::LineJoinType)
!polymorphic_shared_ptr(massif::LineStyle, styles.LineStyle)

!spec(massif::LineStyle, elementstyle, -)

%attribute(massif::LineStyle, float, Width, getWidth)
%attribute(massif::LineStyle, float, ClickWidth, getClickWidth)
%attribute(massif::LineStyle, float, StretchFactor, getStretchFactor)
%attribute(massif::LineStyle, massif::LineJoinType::LineJoinType, LineJoinType, getLineJoinType)
%attribute(massif::LineStyle, massif::LineEndType::LineEndType, LineEndType, getLineEndType)
%attributestring(massif::LineStyle, std::shared_ptr<massif::Bitmap>, Bitmap, getBitmap)
%ignore massif::LineStyle::LineStyle;

%include "styles/LineStyle.h"

#endif
