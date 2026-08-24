#ifndef _LINESTYLEBUILDER_I
#define _LINESTYLEBUILDER_I

%module LineStyleBuilder

!proxy_imports(massif::LineStyleBuilder, graphics.Bitmap, styles.LineStyle, styles.StyleBuilder)

%{
#include "styles/LineStyleBuilder.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/LineStyle.i"
%import "styles/StyleBuilder.i"

!polymorphic_shared_ptr(massif::LineStyleBuilder, styles.LineStyleBuilder)

!spec(massif::LineStyleBuilder, elementstyle, line)

%attribute(massif::LineStyleBuilder, float, Width, getWidth, setWidth)
%attribute(massif::LineStyleBuilder, float, ClickWidth, getClickWidth, setClickWidth)
%attribute(massif::LineStyleBuilder, float, StretchFactor, getStretchFactor, setStretchFactor)
%attribute(massif::LineStyleBuilder, massif::LineJoinType::LineJoinType, LineJoinType, getLineJoinType, setLineJoinType)
%attribute(massif::LineStyleBuilder, massif::LineEndType::LineEndType, LineEndType, getLineEndType, setLineEndType)
%attributestring(massif::LineStyleBuilder, std::shared_ptr<massif::Bitmap>, Bitmap, getBitmap, setBitmap)
%std_exceptions(massif::LineStyleBuilder::setBitmap)

%include "styles/LineStyleBuilder.h"

#endif
