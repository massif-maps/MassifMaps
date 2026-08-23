#ifndef _MARKERSTYLE_I
#define _MARKERSTYLE_I

%module MarkerStyle

!proxy_imports(massif::MarkerStyle, graphics.Bitmap, graphics.Color, styles.BillboardStyle)

%{
#include "styles/MarkerStyle.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "graphics/Bitmap.i"
%import "styles/BillboardStyle.i"

!polymorphic_shared_ptr(massif::MarkerStyle, styles.MarkerStyle)


!spec(massif::MarkerStyle, elementstyle, -)
%attribute(massif::MarkerStyle, float, Size, getSize)
%attribute(massif::MarkerStyle, float, ClickSize, getClickSize)
%attribute(massif::MarkerStyle, massif::BillboardOrientation::BillboardOrientation, OrientationMode, getOrientationMode)
%attribute(massif::MarkerStyle, massif::BillboardScaling::BillboardScaling, ScalingMode, getScalingMode)
%attribute(massif::MarkerStyle, float, AnchorPointX, getAnchorPointX)
%attribute(massif::MarkerStyle, float, AnchorPointY, getAnchorPointY)
%attributestring(massif::MarkerStyle, std::shared_ptr<massif::Bitmap>, Bitmap, getBitmap)
%ignore massif::MarkerStyle::MarkerStyle;

%include "styles/MarkerStyle.h"

#endif
