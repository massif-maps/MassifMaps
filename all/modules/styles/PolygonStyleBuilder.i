#ifndef _POLYGONSTYLEBUILDER_I
#define _POLYGONSTYLEBUILDER_I

%module PolygonStyleBuilder

!proxy_imports(massif::PolygonStyleBuilder, styles.PolygonStyle, styles.StyleBuilder)

%{
#include "styles/PolygonStyleBuilder.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/PolygonStyle.i"
%import "styles/StyleBuilder.i"

!polymorphic_shared_ptr(massif::PolygonStyleBuilder, styles.PolygonStyleBuilder)

!spec(massif::PolygonStyleBuilder, elementstyle, polygon)

%attributestring(massif::PolygonStyleBuilder, std::shared_ptr<massif::LineStyle>, LineStyle, getLineStyle, setLineStyle)

%include "styles/PolygonStyleBuilder.h"

#endif
