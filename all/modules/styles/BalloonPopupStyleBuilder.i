#ifndef _BALLOONPOPUPSTYLEBUILDER_I
#define _BALLOONPOPUPSTYLEBUILDER_I

%module BalloonPopupStyleBuilder

!proxy_imports(massif::BalloonPopupStyleBuilder, graphics.Color, graphics.Bitmap, styles.PopupStyleBuilder, styles.BalloonPopupStyle)

%{
#include "styles/BalloonPopupStyleBuilder.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "graphics/Color.i"
%import "graphics/Bitmap.i"
%import "styles/PopupStyleBuilder.i"
%import "styles/BalloonPopupStyle.i"

!polymorphic_shared_ptr(massif::BalloonPopupStyleBuilder, styles.BalloonPopupStyleBuilder)


!spec(massif::BalloonPopupStyleBuilder, elementstyle, balloon)
%attribute(massif::BalloonPopupStyleBuilder, int, CornerRadius, getCornerRadius, setCornerRadius)
%attributeval(massif::BalloonPopupStyleBuilder, massif::Color, LeftColor, getLeftColor, setLeftColor)
%attributestring(massif::BalloonPopupStyleBuilder, std::shared_ptr<massif::Bitmap>, LeftImage, getLeftImage, setLeftImage)
%attributeval(massif::BalloonPopupStyleBuilder, massif::BalloonPopupMargins, LeftMargins, getLeftMargins, setLeftMargins)
%attributeval(massif::BalloonPopupStyleBuilder, massif::Color, RightColor, getRightColor, setRightColor)
%attributestring(massif::BalloonPopupStyleBuilder, std::shared_ptr<massif::Bitmap>, RightImage, getRightImage, setRightImage)
%attributeval(massif::BalloonPopupStyleBuilder, massif::BalloonPopupMargins, RightMargins, getRightMargins, setRightMargins)
%attributeval(massif::BalloonPopupStyleBuilder, massif::Color, TitleColor, getTitleColor, setTitleColor)
%attributestring(massif::BalloonPopupStyleBuilder, std::string, TitleFontName, getTitleFontName, setTitleFontName)
%attributestring(massif::BalloonPopupStyleBuilder, std::string, TitleField, getTitleField, setTitleField)
%attribute(massif::BalloonPopupStyleBuilder, int, TitleFontSize, getTitleFontSize, setTitleFontSize)
%attributeval(massif::BalloonPopupStyleBuilder, massif::BalloonPopupMargins, TitleMargins, getTitleMargins, setTitleMargins)
%attribute(massif::BalloonPopupStyleBuilder, bool, TitleWrap, isTitleWrap, setTitleWrap)
%attributeval(massif::BalloonPopupStyleBuilder, massif::Color, DescriptionColor, getDescriptionColor, setDescriptionColor)
%attributestring(massif::BalloonPopupStyleBuilder, std::string, DescriptionFontName, getDescriptionFontName, setDescriptionFontName)
%attributestring(massif::BalloonPopupStyleBuilder, std::string, DescriptionField, getDescriptionField, setDescriptionField)
%attribute(massif::BalloonPopupStyleBuilder, int, DescriptionFontSize, getDescriptionFontSize, setDescriptionFontSize)
%attributeval(massif::BalloonPopupStyleBuilder, massif::BalloonPopupMargins, DescriptionMargins, getDescriptionMargins, setDescriptionMargins)
%attribute(massif::BalloonPopupStyleBuilder, bool, DescriptionWrap, isDescriptionWrap, setDescriptionWrap)
%attributeval(massif::BalloonPopupStyleBuilder, massif::BalloonPopupMargins, ButtonMargins, getButtonMargins, setButtonMargins)
%attributeval(massif::BalloonPopupStyleBuilder, massif::Color, StrokeColor, getStrokeColor, setStrokeColor)
%attribute(massif::BalloonPopupStyleBuilder, int, StrokeWidth, getStrokeWidth, setStrokeWidth)
%attribute(massif::BalloonPopupStyleBuilder, int, TriangleWidth, getTriangleWidth, setTriangleWidth)
%attribute(massif::BalloonPopupStyleBuilder, int, TriangleHeight, getTriangleHeight, setTriangleHeight)
%csmethodmodifiers massif::BalloonPopupStyleBuilder::buildStyle "public new";

%include "styles/BalloonPopupStyleBuilder.h"

#endif
