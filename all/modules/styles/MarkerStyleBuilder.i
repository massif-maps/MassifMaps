#ifndef _MARKERSTYLEBUILDER_I
#define _MARKERSTYLEBUILDER_I

%module MarkerStyleBuilder

!proxy_imports(massif::MarkerStyleBuilder, graphics.Bitmap, styles.BillboardStyleBuilder, styles.MarkerStyle)

%{
#include "styles/MarkerStyleBuilder.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/BillboardStyleBuilder.i"
%import "styles/MarkerStyle.i"

!polymorphic_shared_ptr(massif::MarkerStyleBuilder, styles.MarkerStyleBuilder)


!spec(massif::MarkerStyleBuilder, elementstyle, marker)
%attribute(massif::MarkerStyleBuilder, float, Size, getSize, setSize)
%attribute(massif::MarkerStyleBuilder, float, ClickSize, getClickSize, setClickSize)
%attribute(massif::MarkerStyleBuilder, massif::BillboardOrientation::BillboardOrientation, OrientationMode, getOrientationMode, setOrientationMode)
%attribute(massif::MarkerStyleBuilder, massif::BillboardScaling::BillboardScaling, ScalingMode, getScalingMode, setScalingMode)
%attribute(massif::MarkerStyleBuilder, float, AnchorPointX, getAnchorPointX, setAnchorPointX)
%attribute(massif::MarkerStyleBuilder, float, AnchorPointY, getAnchorPointY, setAnchorPointY)
%attributestring(massif::MarkerStyleBuilder, std::shared_ptr<massif::Bitmap>, Bitmap, getBitmap, setBitmap)
%std_exceptions(massif::MarkerStyleBuilder::setBitmap)
!objc_rename(setAnchorPointX) massif::MarkerStyleBuilder::setAnchorPoint(float, float);

%include "styles/MarkerStyleBuilder.h"

#endif
