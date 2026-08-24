#ifndef _TEXTSTYLEBUILDER_I
#define _TEXTSTYLEBUILDER_I

%module TextStyleBuilder

!proxy_imports(massif::TextStyleBuilder, graphics.Color, graphics.Bitmap, styles.LabelStyleBuilder, styles.TextStyle)

%{
#include "styles/TextStyleBuilder.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/LabelStyleBuilder.i"
%import "styles/TextStyle.i"

!polymorphic_shared_ptr(massif::TextStyleBuilder, styles.TextStyleBuilder)

!spec(massif::TextStyleBuilder, elementstyle, text)

%attributestring(massif::TextStyleBuilder, std::string, FontName, getFontName, setFontName)
%attributestring(massif::TextStyleBuilder, std::string, TextField, getTextField, setTextField)
%attribute(massif::TextStyleBuilder, float, FontSize, getFontSize, setFontSize)
%attribute(massif::TextStyleBuilder, bool, BreakLines, isBreakLines, setBreakLines)
%attributeval(massif::TextStyleBuilder, massif::TextMargins, TextMargins, getTextMargins, setTextMargins)
%attributeval(massif::TextStyleBuilder, massif::Color, StrokeColor, getStrokeColor, setStrokeColor)
%attribute(massif::TextStyleBuilder, float, StrokeWidth, getStrokeWidth, setStrokeWidth)
%attributeval(massif::TextStyleBuilder, massif::Color, BorderColor, getBorderColor, setBorderColor)
%attribute(massif::TextStyleBuilder, float, BorderWidth, getBorderWidth, setBorderWidth)
%attributeval(massif::TextStyleBuilder, massif::Color, BackgroundColor, getBackgroundColor, setBackgroundColor)
%csmethodmodifiers massif::TextStyleBuilder::buildStyle "public new";

%include "styles/TextStyleBuilder.h"

#endif
