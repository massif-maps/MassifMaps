#ifndef _TEXTSTYLE_I
#define _TEXTSTYLE_I

%module TextStyle

!proxy_imports(massif::TextStyle, graphics.Color, styles.LabelStyle)

%{
#include "styles/TextStyle.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/LabelStyle.i"

!value_type(massif::TextMargins, styles.TextMargins)

%attribute(massif::TextMargins, int, Left, getLeft)
%attribute(massif::TextMargins, int, Right, getRight)
%attribute(massif::TextMargins, int, Top, getTop)
%attribute(massif::TextMargins, int, Bottom, getBottom)

!polymorphic_shared_ptr(massif::TextStyle, styles.TextStyle)

!spec(massif::TextStyle, elementstyle, -)

%attributeval(massif::TextStyle, massif::Color, FontColor, getFontColor)
%attributestring(massif::TextStyle, std::string, FontName, getFontName)
%attributestring(massif::TextStyle, std::string, TextField, getTextField)
%attribute(massif::TextStyle, float, FontSize, getFontSize)
%attribute(massif::TextStyle, bool, BreakLines, isBreakLines)
%attributeval(massif::TextStyle, massif::TextMargins, TextMargins, getTextMargins)
%attributeval(massif::TextStyle, massif::Color, StrokeColor, getStrokeColor)
%attribute(massif::TextStyle, float, StrokeWidth, getStrokeWidth)
%attributeval(massif::TextStyle, massif::Color, BorderColor, getBorderColor)
%attribute(massif::TextStyle, float, BorderWidth, getBorderWidth)
%attributeval(massif::TextStyle, massif::Color, BackgroundColor, getBackgroundColor)
%ignore massif::TextStyle::TextStyle;

%include "styles/TextStyle.h"

#endif
