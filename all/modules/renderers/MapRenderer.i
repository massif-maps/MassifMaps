#ifndef _MAPRENDERER_I
#define _MAPRENDERER_I

%module MapRenderer

!proxy_imports(massif::MapRenderer, core.MapPos, core.MapBounds, core.ScreenPos, graphics.ViewState, renderers.MapRendererListener, renderers.PostProcessEffect, renderers.RendererCaptureListener, renderers.RedrawRequestListener)

%{
#include "renderers/MapRenderer.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <massifswig.i>

%import "core/MapPos.i"
%import "core/MapBounds.i"
%import "core/ScreenPos.i"
%import "graphics/ViewState.i"
%import "renderers/MapRendererListener.i"
%import "renderers/PostProcessEffect.i"
%import "renderers/RendererCaptureListener.i"
%import "renderers/RedrawRequestListener.i"

!shared_ptr(massif::MapRenderer, renderers.MapRenderer)

%attributestring(massif::MapRenderer, std::shared_ptr<massif::MapRendererListener>, MapRendererListener, getMapRendererListener, setMapRendererListener)
%std_exceptions(massif::MapRenderer::captureRendering)
%ignore massif::MapRenderer::MapRenderer;
%ignore massif::MapRenderer::init;
%ignore massif::MapRenderer::deinit;
%ignore massif::MapRenderer::getLayers;
%ignore massif::MapRenderer::getGLResourceManager;
%ignore massif::MapRenderer::getFrameFog;
%ignore massif::MapRenderer::getBillboardDrawDatas;
%ignore massif::MapRenderer::getProjectionSurface;
%ignore massif::MapRenderer::getAnimationHandler;
%ignore massif::MapRenderer::getKineticEventHandler;
%ignore massif::MapRenderer::getRedrawRequestListener;
%ignore massif::MapRenderer::setRedrawRequestListener;
%ignore massif::MapRenderer::calculateCameraEvent;
%ignore massif::MapRenderer::moveToFitBounds;
%ignore massif::MapRenderer::screenToWorld;
%ignore massif::MapRenderer::worldToScreen;
%ignore massif::MapRenderer::onSurfaceCreated;
%ignore massif::MapRenderer::onSurfaceChanged;
%ignore massif::MapRenderer::onDrawFrame;
%ignore massif::MapRenderer::onSurfaceDestroyed;
%ignore massif::MapRenderer::finishRendering;
%ignore massif::MapRenderer::clearAndBindScreenFBO;
%ignore massif::MapRenderer::blendAndUnbindScreenFBO;
%ignore massif::MapRenderer::setZBuffering;
%ignore massif::MapRenderer::calculateRayIntersectedElements;
%ignore massif::MapRenderer::billboardsChanged;
%ignore massif::MapRenderer::vtLabelsChanged;
%ignore massif::MapRenderer::layerChanged;
%ignore massif::MapRenderer::viewChanged;
%ignore massif::MapRenderer::registerOnChangeListener;
%ignore massif::MapRenderer::unregisterOnChangeListener;
%ignore massif::MapRenderer::addRenderThreadCallback;
%ignore massif::MapRenderer::getOptions;

!standard_equals(massif::MapRenderer);

%include "renderers/MapRenderer.h"

#endif
