#ifndef _BALLOONPOPUP_I
#define _BALLOONPOPUP_I

%module BalloonPopup

!proxy_imports(massif::BalloonPopup, core.MapPos, core.ScreenPos, graphics.Bitmap, geometry.Geometry, styles.BalloonPopupStyle, ui.ClickInfo, vectorelements.BalloonPopupButton, vectorelements.BalloonPopupEventListener, vectorelements.Popup)
!java_imports(massif::BalloonPopup, com.massifmaps.ui.ClickType)

%{
#include "vectorelements/BalloonPopup.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/ScreenPos.i"
%import "graphics/Bitmap.i"
%import "styles/BalloonPopupStyle.i"
%import "vectorelements/BalloonPopupEventListener.i"
%import "vectorelements/Popup.i"

!polymorphic_shared_ptr(massif::BalloonPopup, vectorelements.BalloonPopup)


!spec(massif::BalloonPopup, element, balloon, alias(position, pos), alias(description, desc))
%attributestring(massif::BalloonPopup, std::string, Title, getTitle, setTitle)
%attributestring(massif::BalloonPopup, std::string, Description, getDescription, setDescription)
%csmethodmodifiers massif::BalloonPopup::Style "public new";
!attributestring_polymorphic(massif::BalloonPopup, styles.BalloonPopupStyle, Style, getStyle, setStyle)
!attributestring_polymorphic(massif::BalloonPopup, vectorelements.BalloonPopupEventListener, BalloonPopupEventListener, getBalloonPopupEventListener, setBalloonPopupEventListener)
%std_exceptions(massif::BalloonPopup::BalloonPopup)
%std_exceptions(massif::BalloonPopup::setStyle)
%std_exceptions(massif::BalloonPopup::addButton)
%std_exceptions(massif::BalloonPopup::removeButton)

%include "vectorelements/BalloonPopup.h"

#endif
