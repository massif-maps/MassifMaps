#ifndef _CELESTIALLABEL_I
#define _CELESTIALLABEL_I

%module(directors="1") CelestialLabel

!proxy_imports(massif::CelestialLabel, celestial.CelestialObject, graphics.Color)

%{
#include "celestial/CelestialLabel.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "celestial/CelestialObject.i"
%import "graphics/Color.i"

!polymorphic_shared_ptr(massif::CelestialLabel, celestial.CelestialLabel)
!spec(massif::CelestialLabel, celestial, label)
!method(massif::CelestialLabel, setAnchorPoint, arg(x, float), arg(y, float), returns(void))
!method(massif::CelestialLabel, setOffset, arg(x, float), arg(y, float), returns(void))

%attributestring(massif::CelestialLabel, std::string, Text, getText, setText)
%attributestring(massif::CelestialLabel, std::string, FontName, getFontName, setFontName)
%attribute(massif::CelestialLabel, float, FontSize, getFontSize, setFontSize)
%attributeval(massif::CelestialLabel, massif::Color, TextColor, getTextColor, setTextColor)
%attributeval(massif::CelestialLabel, massif::Color, HaloColor, getHaloColor, setHaloColor)
%attribute(massif::CelestialLabel, float, HaloWidth, getHaloWidth, setHaloWidth)
%attributeval(massif::CelestialLabel, massif::Color, BackgroundColor, getBackgroundColor, setBackgroundColor)
%attribute(massif::CelestialLabel, float, BackgroundRadius, getBackgroundRadius, setBackgroundRadius)
%attribute(massif::CelestialLabel, float, PaddingX, getPaddingX, setPaddingX)
%attribute(massif::CelestialLabel, float, PaddingY, getPaddingY, setPaddingY)
%attribute(massif::CelestialLabel, float, AnchorPointX, getAnchorPointX)
%attribute(massif::CelestialLabel, float, AnchorPointY, getAnchorPointY)
%attribute(massif::CelestialLabel, float, OffsetX, getOffsetX)
%attribute(massif::CelestialLabel, float, OffsetY, getOffsetY)
%attribute(massif::CelestialLabel, bool, Clickable, isClickable, setClickable)
%ignore massif::CelestialLabel::buildBitmap;
%std_exceptions(massif::CelestialLabel::CelestialLabel)

%include "celestial/CelestialLabel.h"

#endif
