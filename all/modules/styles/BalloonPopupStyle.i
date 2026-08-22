#ifndef _BALLOONPOPUPSTYLE_I
#define _BALLOONPOPUPSTYLE_I

%module BalloonPopupStyle

!proxy_imports(massif::BalloonPopupStyle, graphics.Color, graphics.Bitmap, styles.PopupStyle)

%{
#include "styles/BalloonPopupStyle.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/PopupStyle.i"

!value_type(massif::BalloonPopupMargins, styles.BalloonPopupMargins)

%attribute(massif::BalloonPopupMargins, int, Left, getLeft)
%attribute(massif::BalloonPopupMargins, int, Right, getRight)
%attribute(massif::BalloonPopupMargins, int, Top, getTop)
%attribute(massif::BalloonPopupMargins, int, Bottom, getBottom)

!polymorphic_shared_ptr(massif::BalloonPopupStyle, styles.BalloonPopupStyle)


!spec(massif::BalloonPopupStyle, elementstyle, -)
%attributeval(massif::BalloonPopupStyle, massif::Color, BackgroundColor, getBackgroundColor)
%attribute(massif::BalloonPopupStyle, int, CornerRadius, getCornerRadius)
%attributeval(massif::BalloonPopupStyle, massif::Color, LeftColor, getLeftColor)
%attributestring(massif::BalloonPopupStyle, std::shared_ptr<massif::Bitmap>, LeftImage, getLeftImage)
%attributeval(massif::BalloonPopupStyle, massif::BalloonPopupMargins, LeftMargins, getLeftMargins)
%attributeval(massif::BalloonPopupStyle, massif::Color, RightColor, getRightColor)
%attributestring(massif::BalloonPopupStyle, std::shared_ptr<massif::Bitmap>, RightImage, getRightImage)
%attributeval(massif::BalloonPopupStyle, massif::BalloonPopupMargins, RightMargins, getRightMargins)
%attributeval(massif::BalloonPopupStyle, massif::Color, TitleColor, getTitleColor)
%attributestring(massif::BalloonPopupStyle, std::string, TitleFontName, getTitleFontName)
%attributestring(massif::BalloonPopupStyle, std::string, TitleField, getTitleField)
%attribute(massif::BalloonPopupStyle, int, TitleFontSize, getTitleFontSize)
%attributeval(massif::BalloonPopupStyle, massif::BalloonPopupMargins, TitleMargins, getTitleMargins)
%attribute(massif::BalloonPopupStyle, bool, TitleWrap, isTitleWrap)
%attributeval(massif::BalloonPopupStyle, massif::Color, DescriptionColor, getDescriptionColor)
%attributestring(massif::BalloonPopupStyle, std::string, DescriptionFontName, getDescriptionFontName)
%attributestring(massif::BalloonPopupStyle, std::string, DescriptionField, getDescriptionField)
%attribute(massif::BalloonPopupStyle, int, DescriptionFontSize, getDescriptionFontSize)
%attributeval(massif::BalloonPopupStyle, massif::BalloonPopupMargins, DescriptionMargins, getDescriptionMargins)
%attribute(massif::BalloonPopupStyle, bool, DescriptionWrap, isDescriptionWrap)
%attributeval(massif::BalloonPopupStyle, massif::BalloonPopupMargins, ButtonMargins, getButtonMargins)
%attributeval(massif::BalloonPopupStyle, massif::Color, StrokeColor, getStrokeColor)
%attribute(massif::BalloonPopupStyle, int, StrokeWidth, getStrokeWidth)
%attribute(massif::BalloonPopupStyle, int, TriangleWidth, getTriangleWidth)
%attribute(massif::BalloonPopupStyle, int, TriangleHeight, getTriangleHeight)
%ignore massif::BalloonPopupStyle::BalloonPopupStyle;

%include "styles/BalloonPopupStyle.h"

#endif
