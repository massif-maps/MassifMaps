#ifndef _LAYERS_I
#define _LAYERS_I

%module Layers

!proxy_imports(massif::Layers, layers.Layer, layers.LayerVector)

%{
#include "components/Layers.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "layers/Layer.i"

!shared_ptr(massif::Layers, components.Layers)

// A spec builds a layer, it does not place it. This is how one reaches the map - and how an app
// that orders its stack by kind puts one at a given index rather than only on top.
!method(massif::Layers, add, arg(layer, handle), returns(void))
!method(massif::Layers, remove, arg(layer, handle), returns(bool))
!method(massif::Layers, insert, arg(index, int), arg(layer, handle), returns(void))
!method(massif::Layers, set, arg(index, int), arg(layer, handle), returns(void))
!method(massif::Layers, get, arg(index, int), returns(object, massif::Layer))
!method(massif::Layers, clear, returns(void))
%typemap(cscode) massif::Layers %{ public Layer this[int index] { get { return Get(index); } set { Set(index, value); } } %}

%csmethodmodifiers massif::Layers::get "private";
%csmethodmodifiers massif::Layers::set "private";

%attribute(massif::Layers, int, Count, count)
%std_exceptions(massif::Layers::get)
%std_exceptions(massif::Layers::set)
%std_exceptions(massif::Layers::insert)
%ignore massif::Layers::Layers;
!standard_equals(massif::Layers);

%include "components/Layers.h"

#endif
