#ifndef _NMLMODELLODTREEEVENTLISTENER_I
#define _NMLMODELLODTREEEVENTLISTENER_I

%module(directors="1") NMLModelLODTreeEventListener

#ifdef _MASSIF_NMLMODELLODTREE_SUPPORT

!proxy_imports(massif::NMLModelLODTreeEventListener, ui.NMLModelLODTreeClickInfo)

%{
#include "layers/NMLModelLODTreeEventListener.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "ui/NMLModelLODTreeClickInfo.i"

!polymorphic_shared_ptr(massif::NMLModelLODTreeEventListener, layers.NMLModelLODTreeEventListener)

%feature("director") massif::NMLModelLODTreeEventListener;

%include "layers/NMLModelLODTreeEventListener.h"

#endif

#endif
