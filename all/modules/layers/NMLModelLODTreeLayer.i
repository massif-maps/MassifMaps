#ifndef _NMLMODELLODTREELAYER_I
#define _NMLMODELLODTREELAYER_I

%module NMLModelLODTreeLayer

#ifdef _MASSIF_NMLMODELLODTREE_SUPPORT

!proxy_imports(massif::NMLModelLODTreeLayer, datasources.NMLModelLODTreeDataSource, layers.Layer, layers.NMLModelLODTreeEventListener)

%{
#include "layers/NMLModelLODTreeLayer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "datasources/NMLModelLODTreeDataSource.i"
%import "layers/NMLModelLODTreeEventListener.i"
%import "layers/Layer.i"

!polymorphic_shared_ptr(massif::NMLModelLODTreeLayer, layers.NMLModelLODTreeLayer)

!spec(massif::NMLModelLODTreeLayer, layer, nml-lodtree, alias(source, dataSource))

!attributestring_polymorphic(massif::NMLModelLODTreeLayer, datasources.NMLModelLODTreeDataSource, DataSource, getDataSource);
%attribute(massif::NMLModelLODTreeLayer, std::size_t, MaxMemorySize, getMaxMemorySize, setMaxMemorySize)
%attribute(massif::NMLModelLODTreeLayer, float, LODResolutionFactor, getLODResolutionFactor, setLODResolutionFactor)
%std_exceptions(massif::NMLModelLODTreeLayer::NMLModelLODTreeLayer)

%include "layers/NMLModelLODTreeLayer.h"

#endif

#endif
