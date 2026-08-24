#ifndef _POINTSTYLEBUILDER_I
#define _POINTSTYLEBUILDER_I

%module PointStyleBuilder

!proxy_imports(massif::PointStyleBuilder, graphics.Bitmap, styles.PointStyle, styles.StyleBuilder)

%{
#include "styles/PointStyleBuilder.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/PointStyle.i"
%import "styles/StyleBuilder.i"

!polymorphic_shared_ptr(massif::PointStyleBuilder, styles.PointStyleBuilder)

!spec(massif::PointStyleBuilder, elementstyle, point)

%attribute(massif::PointStyleBuilder, float, Size, getSize, setSize)
%attribute(massif::PointStyleBuilder, float, ClickSize, getClickSize, setClickSize)
%attributestring(massif::PointStyleBuilder, std::shared_ptr<massif::Bitmap>, Bitmap, getBitmap, setBitmap)
%std_exceptions(massif::PointStyleBuilder::setBitmap)

%include "styles/PointStyleBuilder.h"

#endif
