#ifndef _LAYER_I
#define _LAYER_I

#pragma SWIG nowarn=401

%module Layer

!proxy_imports(massif::Layer, core.MapRange, core.ScreenPos, core.Variant, core.StringVariantMap, graphics.ViewState, renderers.components.CullState, ui.ClickInfo)
!java_imports(massif::Layer, com.massifmaps.ui.ClickType)

%{
#include "layers/Layer.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapRange.i"
%import "core/ScreenPos.i"
%import "core/Variant.i"
%import "graphics/ViewState.i"
%import "renderers/components/CullState.i"
%import "ui/ClickInfo.i"

!polymorphic_shared_ptr(massif::Layer, layers.Layer)
!method(massif::Layer, refresh, returns(void))
!value_type(std::vector<std::shared_ptr<massif::Layer> >, layers.LayerVector)

%attributeval(massif::Layer, %arg(std::map<std::string, massif::Variant>), MetaData, getMetaData, setMetaData)
%attribute(massif::Layer, int, UpdatePriority, getUpdatePriority, setUpdatePriority)
%attribute(massif::Layer, int, CullDelay, getCullDelay, setCullDelay)
%attribute(massif::Layer, bool, Visible, isVisible, setVisible)
%attributeval(massif::Layer, massif::MapRange, VisibleZoomRange, getVisibleZoomRange, setVisibleZoomRange)
%attribute(massif::Layer, float, Opacity, getOpacity, setOpacity)
%attribute(massif::Layer, bool, PostProcessed, isPostProcessed, setPostProcessed)
%ignore massif::Layer::onDrawFrame;
%ignore massif::Layer::onDrawFrame3D;
%ignore massif::Layer::getBackgroundBitmap;
%ignore massif::Layer::getStyleEnvironment;
%ignore massif::Layer::getSkyBitmap;
%ignore massif::Layer::calculateRayIntersectedElements;
%ignore massif::Layer::registerDataSourceListener;
%ignore massif::Layer::unregisterDataSourceListener;
%ignore massif::Layer::getCullDelay;
%ignore massif::Layer::getLastCullState;
!standard_equals(massif::Layer);

%include "layers/Layer.h"

!value_template(std::vector<std::shared_ptr<massif::Layer> >, layers.LayerVector)

#endif
