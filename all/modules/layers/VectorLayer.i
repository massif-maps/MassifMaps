#ifndef _VECTORLAYER_I
#define _VECTORLAYER_I

%module VectorLayer

!proxy_imports(massif::VectorLayer, datasources.VectorDataSource, layers.Layer, layers.VectorElementEventListener)

%{
#include "layers/VectorLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "layers/VectorElementEventListener.i"
%import "layers/Layer.i"
%import "datasources/VectorDataSource.i"

!polymorphic_shared_ptr(massif::VectorLayer, layers.VectorLayer)


!event(massif::VectorLayer, vectorelement.clicked, payload(massif::VectorElementClickInfo), consumable)
!spec(massif::VectorLayer, layer, elements, alias(source, dataSource))
!attributestring_polymorphic(massif::VectorLayer, datasources.VectorDataSource, DataSource, getDataSource)
!attributestring_polymorphic(massif::VectorLayer, layers.VectorElementEventListener, VectorElementEventListener, getVectorElementEventListener, setVectorElementEventListener)
%attribute(massif::VectorLayer, bool, ZBuffering, isZBuffering, setZBuffering)
%std_exceptions(massif::VectorLayer::VectorLayer)

%include "layers/VectorLayer.h"

#endif
