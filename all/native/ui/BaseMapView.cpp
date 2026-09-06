#include "BaseMapView.h"
#include "components/CancelableThreadPool.h"
#include "components/Layers.h"
#include "core/MapPos.h"
#include "core/MapBounds.h"
#include "core/ScreenPos.h"
#include "core/ScreenBounds.h"
#include "layers/Layer.h"
#include "layers/TileLayer.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "renderers/MapRenderer.h"
#include "renderers/cameraevents/CameraPanEvent.h"
#include "renderers/cameraevents/CameraRotationEvent.h"
#include "renderers/cameraevents/CameraTiltEvent.h"
#include "renderers/cameraevents/CameraZoomEvent.h"
#include "renderers/cameraevents/CameraPanEvent.h"
#include "ui/TouchHandler.h"
#include "utils/PlatformUtils.h"
#include "utils/Log.h"

#include <list>
#include <unordered_map>
#include <vector>
#include <sstream>

namespace {
    // Van Wijk & Nuij's rho, the aggressiveness of the pull-back over a long move; 1.42 is the
    // value they derive as optimal, and neither they nor any port of it exposes another.
    const float FLIGHT_RHO = 1.42f;
}

namespace massif {

    std::string BaseMapView::GetSDKVersion() {
        std::stringstream ss;
        ss << "Build: " << PlatformUtils::GetPlatformId() << "-" << PlatformUtils::GetSDKVersion();
        ss << ", time: " << __DATE__ << " " << __TIME__;
        ss << ", device type: " << PlatformUtils::GetDeviceType();
        ss << ", device OS: " << PlatformUtils::GetDeviceOS();
        return ss.str();
    }
    
    BaseMapView::BaseMapView() :
        _envelopeThreadPool(std::make_shared<CancelableThreadPool>()),
        _tileThreadPool(std::make_shared<CancelableThreadPool>()),
        _options(std::make_shared<Options>(_envelopeThreadPool, _tileThreadPool)),
        _layers(std::make_shared<Layers>(_envelopeThreadPool, _tileThreadPool, _options)),
        _mapRenderer(std::make_shared<MapRenderer>(_layers, _options)),
        _touchHandler(std::make_shared<TouchHandler>(_mapRenderer, _options)),
        _mutex()
    {
        _mapRenderer->init();
        _touchHandler->init();
        _layers->setComponents(_mapRenderer, _touchHandler);
        
        setFocusPos(MapPos(), 0);
        setRotation(0, 0);
        setTilt(90, 0);
        setZoom(0, 0);
        _mapRenderer->resetCameraPlaced(); // the default view is the SDK's, not the app's camera

        Log::Infof("BaseMapView: %s", GetSDKVersion().c_str());
    }
    
    BaseMapView::~BaseMapView() {
        // Set stop flag and detach every thread, once the thread quits
        // all objects they hold will be released
        _envelopeThreadPool->deinit();
        _tileThreadPool->deinit();
        _mapRenderer->deinit();
        _touchHandler->deinit();
    }
    
    void BaseMapView::onSurfaceCreated() {
        Log::Info("BaseMapView::onSurfaceCreated()");
        _mapRenderer->onSurfaceCreated();
    }
    
    void BaseMapView::onSurfaceChanged(int width, int height) {
        Log::Infof("BaseMapView::onSurfaceChanged(): width: %d, height: %d", width, height);
        _mapRenderer->onSurfaceChanged(width, height);
    }
    
    void BaseMapView::onDrawFrame() {
        _mapRenderer->onDrawFrame();
    }
    
    void BaseMapView::onSurfaceDestroyed() {
        Log::Info("BaseMapView::onSurfaceDestroyed()");
        _mapRenderer->onSurfaceDestroyed();
    }

    void BaseMapView::finishRendering() {
        _mapRenderer->finishRendering();
    }
    
    void BaseMapView::onInputEvent(int event, float x1, float y1, float x2, float y2) {
        _touchHandler->onTouchEvent(event, ScreenPos(x1, y1), ScreenPos(x2, y2));
    }
    
    void BaseMapView::onWheelEvent(int delta, float x, float y) {
        _touchHandler->onWheelEvent(delta, ScreenPos(x, y));
    }
    
    MapPos BaseMapView::getFocusPos() const {
        MapPos mapPosInternal = _options->getProjectionSurface()->calculateMapPos(_mapRenderer->getViewState().getFocusPos());
        return _options->getBaseProjection()->fromInternal(mapPosInternal);
    }
    
    MapPos BaseMapView::getCameraPos() const {
        MapPos mapPosInternal = _options->getProjectionSurface()->calculateMapPos(_mapRenderer->getViewState().getCameraPos());
        // The GROUND under the camera. calculateMapPos carries the height through, and a focus
        // position with a height in it moves the view - setFocusPos would frame somewhere else.
        mapPosInternal.setZ(0);
        return _options->getBaseProjection()->fromInternal(mapPosInternal);
    }

    float BaseMapView::getRotation() const {
        return _mapRenderer->getViewState().getRotation();
    }
    
    float BaseMapView::getTilt() const {
        return _mapRenderer->getViewState().getTilt();
    }
    
    float BaseMapView::getZoom() const {
        return _mapRenderer->getViewState().getZoom();
    }
    
    void BaseMapView::pan(const MapVec& deltaPos, float durationSeconds) {
        MapPos focusPos0Internal = _options->getBaseProjection()->toInternal(getFocusPos());
        MapPos focusPos1Internal = _options->getBaseProjection()->toInternal(getFocusPos() + deltaPos);

        _mapRenderer->getAnimationHandler().stopPan();
        _mapRenderer->getKineticEventHandler().stopPan();
        
        CameraPanEvent cameraEvent;
        cameraEvent.setKeepRotation(true);
        cameraEvent.setPosDelta(std::make_pair(focusPos0Internal, focusPos1Internal));
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
    
    void BaseMapView::setFocusPos(const MapPos& pos, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopPan();
        _mapRenderer->getKineticEventHandler().stopPan();
        
        CameraPanEvent cameraEvent;
        cameraEvent.setKeepRotation(true);
        cameraEvent.setPos(_options->getBaseProjection()->toInternal(pos));
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
    
    void BaseMapView::moveTo(const MapPos& pos, float zoom, const float* rotation,
                             const float* tilt) {
        // ZOOM BEFORE THE PAN when zooming in. Restricted panning clamps the focus so the VIEWPORT
        // stays inside the pan bounds, so how hard a target is clamped depends on the zoom it is
        // judged at: from the opening world view the whole of Mercator is on screen and any focus
        // is dragged back to the bounds centre - the equator - and the zoom that follows does not
        // undo it. Zooming out is the other way round, so the pan goes first and the zoom clamps
        // afterwards, which is the wanted behaviour.
        //
        // ONE frame for the four: without the hold the render thread drew in between them, at the
        // world view zoomed in but still straight down - and auto-flattened it.
        std::unique_lock<std::recursive_mutex> hold = _mapRenderer->holdView();
        bool zoomIn = zoom > getZoom();
        if (zoomIn) {
            setZoom(zoom, 0);
        }
        if (rotation) {
            setRotation(*rotation, 0);
        }
        if (tilt) {
            setTilt(*tilt, 0);
        }
        setFocusPos(pos, 0);
        if (!zoomIn) {
            setZoom(zoom, 0);
        }
    }

    void BaseMapView::moveTo(const MapPos& pos, float zoom) {
        moveTo(pos, zoom, nullptr, nullptr);
    }

    void BaseMapView::moveTo(const MapPos& pos, float zoom, float rotation, float tilt) {
        moveTo(pos, zoom, &rotation, &tilt);
    }

    void BaseMapView::flyTo(const MapPos& pos, float zoom, float durationSeconds) {
        stopCameraAnimations();
        _mapRenderer->getAnimationHandler().setFlightTarget(_options->getBaseProjection()->toInternal(pos), zoom, nullptr, nullptr, 0.0f, durationSeconds, FLIGHT_RHO);
        // The flight is stepped by the frame loop, and every other camera call gets its frame from
        // calculateCameraEvent. Without this the flight sits at progress 0 on an on-demand
        // renderer - it starts only if something else happens to redraw.
        _mapRenderer->requestRedraw();
    }

    void BaseMapView::flyTo(const MapPos& pos, float zoom, float rotation, float tilt, float durationSeconds) {
        flyTo(pos, zoom, rotation, tilt, 0.0f, durationSeconds);
    }

    void BaseMapView::flyTo(const MapPos& pos, float zoom, float rotation, float tilt, float climbHeight, float durationSeconds) {
        stopCameraAnimations();
        // The climb is a height in the base projection's units, like the position's Z, so it goes
        // through the same conversion - the internal Z scale is not the internal XY scale.
        MapPos internalPos = _options->getBaseProjection()->toInternal(pos);
        MapPos internalGround = _options->getBaseProjection()->toInternal(MapPos(pos.getX(), pos.getY(), 0));
        MapPos internalClimb = _options->getBaseProjection()->toInternal(MapPos(pos.getX(), pos.getY(), climbHeight));
        double internalClimbHeight = internalClimb.getZ() - internalGround.getZ();
        _mapRenderer->getAnimationHandler().setFlightTarget(internalPos, zoom, &rotation, &tilt, static_cast<float>(internalClimbHeight), durationSeconds, FLIGHT_RHO);
        _mapRenderer->requestRedraw();
    }

    float BaseMapView::getFlightProgress() const {
        return _mapRenderer->getAnimationHandler().getFlightProgress();
    }

    void BaseMapView::stopCameraAnimations() {
        // Whatever was moving the camera has to let go, or it fights the flight for it.
        _mapRenderer->getAnimationHandler().stopPan();
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getAnimationHandler().stopTilt();
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getKineticEventHandler().stopPan();
        _mapRenderer->getKineticEventHandler().stopRotation();
        _mapRenderer->getKineticEventHandler().stopZoom();
    }

    void BaseMapView::stopFlight() {
        _mapRenderer->getAnimationHandler().stopFlight();
    }

    bool BaseMapView::isFlightActive() const {
        return _mapRenderer->getAnimationHandler().isFlightActive();
    }

    void BaseMapView::rotate(float rotationDelta, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getKineticEventHandler().stopRotation();
        
        CameraRotationEvent cameraEvent;
        cameraEvent.setRotationDelta(rotationDelta);
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
        
    void BaseMapView::rotate(float rotationDelta, const MapPos& targetPos, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getKineticEventHandler().stopRotation();
        
        CameraRotationEvent cameraEvent;
        cameraEvent.setRotationDelta(rotationDelta);
        cameraEvent.setTargetPos(_options->getBaseProjection()->toInternal(targetPos));
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
    
    void BaseMapView::setRotation(float rotation, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getKineticEventHandler().stopRotation();
        
        CameraRotationEvent cameraEvent;
        cameraEvent.setRotation(rotation);
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
        
    void BaseMapView::setRotation(float rotation, const MapPos& targetPos, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopRotation();
        _mapRenderer->getKineticEventHandler().stopRotation();
        
        CameraRotationEvent cameraEvent;
        cameraEvent.setRotation(rotation);
        cameraEvent.setTargetPos(_options->getBaseProjection()->toInternal(targetPos));
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
    
    void BaseMapView::tilt(float tiltDelta, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopTilt();
        
        CameraTiltEvent cameraEvent;
        cameraEvent.setKeepRotation(true);
        cameraEvent.setTiltDelta(tiltDelta);
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
    
    void BaseMapView::setTilt(float tilt, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopTilt();
        
        CameraTiltEvent cameraEvent;
        cameraEvent.setKeepRotation(true);
        cameraEvent.setTilt(tilt);
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
    
    void BaseMapView::zoom(float zoomDelta, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getKineticEventHandler().stopZoom();
        
        CameraZoomEvent cameraEvent;
        cameraEvent.setKeepRotation(true);
        cameraEvent.setZoomDelta(zoomDelta);
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
        
    void BaseMapView::zoom(float zoomDelta, const MapPos& targetPos, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getKineticEventHandler().stopZoom();
        
        CameraZoomEvent cameraEvent;
        cameraEvent.setKeepRotation(true);
        cameraEvent.setZoomDelta(zoomDelta);
        cameraEvent.setTargetPos(_options->getBaseProjection()->toInternal(targetPos));
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
    
    void BaseMapView::setZoom(float zoom, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getKineticEventHandler().stopZoom();
        
        CameraZoomEvent cameraEvent;
        cameraEvent.setKeepRotation(true);
        cameraEvent.setZoom(zoom);
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
        
    void BaseMapView::setZoom(float zoom, const MapPos& targetPos, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getKineticEventHandler().stopZoom();
        
        CameraZoomEvent cameraEvent;
        cameraEvent.setKeepRotation(true);
        cameraEvent.setZoom(zoom);
        cameraEvent.setTargetPos(_options->getBaseProjection()->toInternal(targetPos));
        _mapRenderer->calculateCameraEvent(cameraEvent, durationSeconds, false, MapMoveReason::MAP_MOVE_REASON_API);
    }
        
    void BaseMapView::moveToFitBounds(const MapBounds& mapBounds, const ScreenBounds& screenBounds, bool integerZoom, float durationSeconds) {
        moveToFitBounds(mapBounds, screenBounds, integerZoom, false, false, durationSeconds);
    }
        
    void BaseMapView::moveToFitBounds(const MapBounds& mapBounds, const ScreenBounds& screenBounds, bool integerZoom, bool resetRotation, bool resetTilt, float durationSeconds) {
        _mapRenderer->getAnimationHandler().stopZoom();
        _mapRenderer->getKineticEventHandler().stopZoom();
        
        MapBounds mapBoundsInternal(_options->getBaseProjection()->toInternal(mapBounds.getMin()), _options->getBaseProjection()->toInternal(mapBounds.getMax()));
        _mapRenderer->moveToFitBounds(mapBoundsInternal, screenBounds, integerZoom, resetTilt, resetRotation, durationSeconds);
    }
    
    std::shared_ptr<MapEventListener> BaseMapView::getMapEventListener() const {
        return _touchHandler->getMapEventListener();
    }
        
    void BaseMapView::setMapEventListener(const std::shared_ptr<MapEventListener>& mapEventListener) {
        _touchHandler->setMapEventListener(mapEventListener);
    }
        
    std::shared_ptr<RedrawRequestListener> BaseMapView::getRedrawRequestListener() const{
        return _mapRenderer->getRedrawRequestListener();
    }
        
    void BaseMapView::setRedrawRequestListener(const std::shared_ptr<RedrawRequestListener>& listener) {
        _mapRenderer->setRedrawRequestListener(listener);
    }
        
    MapPos BaseMapView::screenToMap(const ScreenPos& screenPos) {
        ViewState viewState = _mapRenderer->getViewState();
        MapPos mapPosInternal = _options->getProjectionSurface()->calculateMapPos(viewState.screenToWorld(cglib::vec2<float>(screenPos.getX(), screenPos.getY()), 0, _options));
        return _options->getBaseProjection()->fromInternal(mapPosInternal);
    }
    
    ScreenPos BaseMapView::mapToScreen(const MapPos& mapPos) {
        ViewState viewState = _mapRenderer->getViewState();
        MapPos mapPosInternal = _options->getBaseProjection()->toInternal(mapPos);
        cglib::vec2<float> screenPos = viewState.worldToScreen(_options->getProjectionSurface()->calculatePosition(mapPosInternal), _options);
        return ScreenPos(screenPos(0), screenPos(1));
    }
    
    void BaseMapView::cancelAllTasks() {
        _envelopeThreadPool->cancelAll();
        _tileThreadPool->cancelAll();
    }
    
    void BaseMapView::clearPreloadingCaches() {
        for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
            if (const std::shared_ptr<TileLayer>& tileLayer = std::dynamic_pointer_cast<TileLayer>(layer)) {
                tileLayer->clearTileCaches(false);
            }
        }
    }
    
    void BaseMapView::clearAllCaches() {
        for (const std::shared_ptr<Layer>& layer : _layers->getAll()) {
            if (const std::shared_ptr<TileLayer>& tileLayer = std::dynamic_pointer_cast<TileLayer>(layer)) {
                tileLayer->clearTileCaches(true);
            }
        }
    }
    
    const std::shared_ptr<Layers>& BaseMapView::getLayers() const {
        return _layers;
    }
    
    const std::shared_ptr<Options>& BaseMapView::getOptions() const {
        return _options;
    }

    const std::shared_ptr<MapRenderer>& BaseMapView::getMapRenderer() const {
        return _mapRenderer;
    }
        
}
