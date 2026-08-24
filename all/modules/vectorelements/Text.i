#ifndef _TEXT_I
#define _TEXT_I

%module Text

!proxy_imports(massif::Text, core.MapPos, graphics.Bitmap, geometry.Geometry, styles.TextStyle, vectorelements.Label)

%{
#include "vectorelements/Text.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "styles/TextStyle.i"
%import "vectorelements/Label.i"

!polymorphic_shared_ptr(massif::Text, vectorelements.Text)

!spec(massif::Text, element, text, alias(position, pos))
%attributestring(massif::Text, std::string, Title, getText, setText)
%csmethodmodifiers massif::Text::Style "public new";
!attributestring_polymorphic(massif::Text, styles.TextStyle, Style, getStyle, setStyle)
%std_exceptions(massif::Text::Text)
%std_exceptions(massif::Text::setStyle)

%include "vectorelements/Text.h"

#endif
