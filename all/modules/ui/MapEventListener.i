#ifndef _MAPEVENTLISTENER_I
#define _MAPEVENTLISTENER_I

%module(directors="1") MapEventListener

!proxy_imports(massif::MapEventListener, ui.MapClickInfo, ui.MapInteractionInfo, ui.MapMoveInfo)

%{
#include "ui/MapEventListener.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "ui/MapClickInfo.i"
%import "ui/MapInteractionInfo.i"
%import "ui/MapMoveInfo.i"

!polymorphic_shared_ptr(massif::MapEventListener, ui.MapEventListener)

%feature("director") massif::MapEventListener;

%include "ui/MapEventListener.h"

#endif
