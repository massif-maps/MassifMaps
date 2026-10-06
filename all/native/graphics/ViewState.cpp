#include "ViewState.h"
#include "projections/EPSG3857.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "terrain/CameraClearance.h"
#include "graphics/ViewDistance.h"
#include "graphics/ZoomConvention.h"
#include "utils/Const.h"
#include "utils/FrameProfiler.h"
#include "utils/GeneralUtils.h"
#include "utils/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <cglib/mat.h>
#include <vt/ViewState.h>

namespace massif {

    // Tangram's far-plane factor on the camera height (core/src/view/view.cpp).
    static const double TANGRAM_FAR_PLANE_FACTOR = 2.0;

    ViewState::ViewState() :
        _cameraPos(0, 0, 1),
        _focusPos(0, 0, 0),
        _upVec(0, 1, 0),
        _cameraChanged(true),
        _rotation(0),
        _tilt(90),
        _cameraTilt(90),
        _zoom(0.0f),
        _2PowZoom(1.0f),
        _zoom0Distance(0.0f),
        _minZoom(0.0f),
        _ignoreMinZoom(false),
        _zoomRange(0.0f, 0.0f),
        _restrictedPanning(false),
        _normalizedResolution(0.0f),
        _width(0),
        _height(0),
        _halfWidth(0.0f),
        _halfHeight(0.0f),
        _aspectRatio(0.0f),
        _screenSizeChanged(false),
        _near(0.0f),
        _far(0.0f),
        _skyVisible(false),
        _skyHorizonNDC(1.0f),
        _fovY(0),
        _halfFOVY(0.0f),
        _tanHalfFOVY(0.0f),
        _cosHalfFOVY(0.0f),
        _tanHalfFOVX(0.0f),
        _cosHalfFOVXY(0.0f),
        _tileDrawSize(0),
        _zoomOffset(0.0f),
        _dpToPX(0),
        _dpi(0),
        _unitToPXCoef(0),
        _unitToDPCoef(0),
        _rotationState(),
        _projectionSurface(),
        _projectionMat(),
        _modelviewMat(),
        _modelviewProjectionMat(),
        _rteModelviewMat(),
        _rteModelviewProjectionMat(),
        _rteSkyProjectionMat(),
        _horizontalLayerOffsetDir(0)
    {
    }
    
    ViewState::~ViewState() {
    }
    
    const cglib::vec3<double>& ViewState::getCameraPos() const {
        return _cameraPos;
    }
    
    void ViewState::setTerrainHeightRange(float minZ, float maxZ) {
        if (minZ != _terrainHeightMin || maxZ != _terrainHeightMax) {
            _terrainHeightMin = minZ;
            _terrainHeightMax = maxZ;
            _cameraChanged = true; // force near/far plane recalculation
        }
    }

    void ViewState::setTerrainCameraReference(double terrainZ, double clearanceFloor, double clearanceFraction) {
        if (terrainZ != _terrainCameraZ) {
            _terrainCameraZ = terrainZ;
            _cameraChanged = true; // the near plane is built on it
        }
        _terrainClearanceFloor = clearanceFloor;
        _terrainClearanceFraction = clearanceFraction;
        _terrainCameraBound = true;
    }

    void ViewState::clearTerrainCameraReference() {
        if (_terrainCameraZ != 0) {
            _terrainCameraZ = 0;
            _cameraChanged = true;
        }
        _terrainClearanceFloor = 0;
        _terrainClearanceFraction = -1;
        _terrainCameraBound = false;
    }

    float ViewState::getTerrainMaxZoom() const {
        // Zooming scales the camera-to-focus vector, so the height that shrinks as 1/2^zoom is above the focus, not z=0.
        if (!_terrainCameraBound || !(_zoom0Distance > 0)) {
            return std::numeric_limits<float>::infinity();
        }
        // Heights are internal units and an orbit is a world distance: on a globe those differ by 2.
        double worldPerInternalZ = worldPerInternal();
        double focusZ = (_projectionSurface ? _projectionSurface->calculateMapPos(_focusPos).getZ() : _focusPos(2));
        double cameraZ = (_projectionSurface ? _projectionSurface->calculateMapPos(_cameraPos).getZ() : _cameraPos(2));
        return CameraClearance::maxZoom(_zoom, focusZ, cameraZ, _terrainCameraZ,
                                        getOrbitDistance(_zoomRange.getMax()) / worldPerInternalZ, _terrainClearanceFloor,
                                        _terrainClearanceFraction);
    }

    float ViewState::getRenderZoom() const {
        return static_cast<float>(ZoomConvention::renderZoom(_zoom, _zoomOffset));
    }

    double ViewState::calculateZoom0Distance(double tanHalfFOVY, const std::shared_ptr<ProjectionSurface>& projectionSurface) const {
        // The surface's world, not WORLD_SIZE: a sphere's equator is twice as wide. The surface is a parameter
        // because on the frame the projection changes the member is still the old one.
        return ZoomConvention::zoom0Distance(_height, Const::WORLD_SIZE * localWorldPerInternal(projectionSurface), _tileDrawSize, _zoomOffset,
                                             tanHalfFOVY, _dpi / Const::UNSCALED_DPI);
    }

    double ViewState::localWorldPerInternal(const std::shared_ptr<ProjectionSurface>& projectionSurface) const {
        if (!projectionSurface) {
            return 1.0;
        }
        double equator = projectionSurface->getWorldWidth() / Const::WORLD_SIZE;
        double local = projectionSurface->calculateLocalScale(_focusPos);
        if (!(local > 0) || local == equator) {
            return equator; // a plane answers the same scale everywhere and never reaches the ramp
        }
        // The local scale frames the same ground as the plane only while the view is a patch: ramp back to the
        // equatorial one as the planet fills the frame, by orbit against radius (a zoom threshold would hide screen size and DPI).
        double radius = projectionSurface->getWorldWidth() / (2 * Const::PI);
        double orbit = ZoomConvention::zoom0Distance(_height, projectionSurface->getWorldWidth(), _tileDrawSize, _zoomOffset,
                                                     _tanHalfFOVY, _dpi / Const::UNSCALED_DPI) / std::pow(2.0, static_cast<double>(_zoom));
        double t = (radius > 0 ? std::max(0.0, std::min(1.0, (4 * radius - orbit) / (3 * radius))) : 1.0);
        return equator + (local - equator) * t;
    }

    double ViewState::getOrbitDistance(float zoom) const {
        return _zoom0Distance / std::pow(2.0, static_cast<double>(zoom));
    }

    double ViewState::getSpanPerZoom() const {
        if (_height <= 0 || !(_zoom0Distance > 0) || !(_tanHalfFOVY > 0)) {
            return 0;
        }
        double unitsPerPixel = 2.0 * _tanHalfFOVY * _zoom0Distance / _height;
        return unitsPerPixel * static_cast<double>(std::max(_width, _height));
    }

    void ViewState::setCameraPos(const cglib::vec3<double>& cameraPos) {
        if (!std::isfinite(cglib::norm(cameraPos))) {
            Log::Errorf("ViewState::setCameraPos: Invalid coordinates %g, %g, %g", cameraPos(0), cameraPos(1), cameraPos(2)); 
            return;
        }
        _cameraPos = cameraPos;
    }
    
    const cglib::vec3<double>& ViewState::getFocusPos() const {
        return _focusPos;
    }
    
    void ViewState::setFocusPos(const cglib::vec3<double>& focusPos) {
        if (!std::isfinite(cglib::norm(focusPos))) {
            Log::Errorf("ViewState::setFocusPos: Invalid coordinates %g, %g, %g", focusPos(0), focusPos(1), focusPos(2)); 
            return;
        }
        _focusPos = focusPos;
    }

    void ViewState::liftFocus(double deltaZ) {
        if (!std::isfinite(deltaZ) || deltaZ == 0) {
            return;
        }
        _focusPos(2) += deltaZ;
        _cameraPos(2) += deltaZ;
        _cameraChanged = true;
    }
    
    void ViewState::setFocusHeight(double internalZ) {
        if (!_projectionSurface || !std::isfinite(internalZ)) {
            return;
        }
        MapPos focusMapPos = _projectionSurface->calculateMapPos(_focusPos);
        if (focusMapPos.getZ() == internalZ) {
            return;
        }
        cglib::vec3<double> focusPos = _projectionSurface->calculatePosition(MapPos(focusMapPos.getX(), focusMapPos.getY(), internalZ));
        cglib::vec3<double> delta = focusPos - _focusPos;
        if (!std::isfinite(cglib::norm(delta))) {
            return;
        }
        _focusPos = focusPos;
        _cameraPos = _cameraPos + delta;
        _cameraChanged = true;
    }

    double ViewState::worldPerInternal() const {
        return localWorldPerInternal(_projectionSurface);
    }

    const cglib::vec3<double>& ViewState::getUpVec() const {
        return _upVec;
    }
    
    void ViewState::setUpVec(const cglib::vec3<double>& upVec) {
        if (!std::isfinite(cglib::norm(upVec))) {
            Log::Errorf("ViewState::setUpVec: Invalid coordinates %g, %g, %g", upVec(0), upVec(1), upVec(2)); 
            return;
        }
        _upVec = upVec;

        if (_projectionSurface) {
            MapVec upVecInternal = _projectionSurface->calculateMapVec(_focusPos, _upVec);
            float rotation = static_cast<float>(-std::atan2(upVecInternal.getX(), upVecInternal.getY()) * Const::RAD_TO_DEG);
            if (!std::isfinite(rotation)) {
                Log::Infof("ViewState::setUpVec: Failed to calculate rotation %g (old %g)", rotation, _rotation);
            } else {
                _rotation = rotation;
            }
        }
    }
    
    float ViewState::getTilt() const {
        return _tilt;
    }
    
    void ViewState::setTilt(float tilt) {
        if (!std::isfinite(tilt)) {
            Log::Errorf("ViewState::setTilt: Invalid value %g", tilt);
            return;
        }
        _tilt = tilt;
        // A negative tilt is a rotation of the view about the camera; setViewTilt is the model where the camera stays.
        _cameraTilt = std::max(tilt, Const::MIN_CAMERA_TILT);
    }

    void ViewState::setViewTilt(float tilt) {
        if (!std::isfinite(tilt)) {
            Log::Errorf("ViewState::setViewTilt: Invalid value %g", tilt);
            return;
        }
        _tilt = tilt;
    }

    float ViewState::getCameraTilt() const {
        return _cameraTilt;
    }

    cglib::vec3<double> ViewState::calculateViewDir() const {
        cglib::vec3<double> viewVec = _focusPos - _cameraPos;
        if (cglib::length(viewVec) == 0) {
            return cglib::vec3<double>::zero();
        }
        viewVec = cglib::unit(viewVec);
        float viewPitch = _cameraTilt - _tilt;
        if (viewPitch == 0) {
            return viewVec;
        }
        cglib::vec3<double> axis = cglib::vector_product(viewVec, _upVec);
        if (cglib::length(axis) == 0) {
            return viewVec;
        }
        return cglib::unit(cglib::transform_vector(viewVec, cglib::rotate4_matrix(axis, viewPitch * Const::DEG_TO_RAD)));
    }
    
    float ViewState::getZoom() const {
        return _zoom;
    }
    
    void ViewState::setZoom(float zoom) {
        if (!std::isfinite(zoom)) {
            Log::Errorf("ViewState::setZoom: Invalid value %g", zoom); 
            return;
        }
        _zoom = zoom;
        _2PowZoom = std::pow(2.0f, zoom);
    }
    
    bool ViewState::isCameraChanged() const {
        return _cameraChanged;
    }
    
    void ViewState::cameraChanged() {
        _cameraChanged = true;
    }
    
    float ViewState::getRotation() const {
        return _rotation;
    }
    
    float ViewState::get2PowZoom() const {
        return _2PowZoom;
    }
    
    float ViewState::getZoom0Distance() const {
        return _zoom0Distance;
    }

    float ViewState::getMinZoom() const {
        return _minZoom;
    }
    
    float ViewState::getNormalizedResolution() const {
        return _normalizedResolution;
    }
    
    int ViewState::getWidth() const {
        return _width;
    }
    
    int ViewState::getHeight() const {
        return _height;
    }
    
    float ViewState::getHalfWidth() const {
        return _halfWidth;
    }
    
    float ViewState::getHalfHeight() const {
        return _halfHeight;
    }
    
    float ViewState::getAspectRatio() const {
        return _aspectRatio;
    }
    
    float ViewState::getNear() const {
        return _near;
    }
    
    float ViewState::getFar() const {
        return _far;
    }
    
    float ViewState::getFOVY() const {
        return _fovY;
    }
    
    float ViewState::getHalfFOVY() const {
        return _halfFOVY;
    }
    
    double ViewState::getTanHalfFOVY() const {
        return _tanHalfFOVY;
    }
    
    double ViewState::getCosHalfFOVY() const {
        return _cosHalfFOVY;
    }
    
    double ViewState::getTanHalfFOVX() const {
        return _tanHalfFOVX;
    }
    
    double ViewState::getCosHalfFOVXY() const {
        return _cosHalfFOVXY;
    }
        
    float ViewState::getDPToPX() const {
        return _dpToPX;
    }
        
    float ViewState::getDPI() const {
        return _dpi;
    }
    
    float ViewState::getUnitToPXCoef() const {
        return _unitToPXCoef;
    }
    
    float ViewState::getUnitToDPCoef() const {
        return _unitToDPCoef;
    }
    
    const ViewState::RotationState& ViewState::getRotationState() const {
        return _rotationState;
    }
    
    std::shared_ptr<ProjectionSurface> ViewState::getProjectionSurface() const {
        return _projectionSurface;
    }
    
    const cglib::mat4x4<double>& ViewState::getProjectionMat() const {
        return _projectionMat;
    }
    
    const cglib::mat4x4<double>& ViewState::getModelviewMat() const {
        return _modelviewMat;
    }
    
    const cglib::mat4x4<double>& ViewState::getModelviewProjectionMat() const {
        return _modelviewProjectionMat;
    }
    
    const cglib::mat4x4<float>& ViewState::getRTEModelviewMat() const {
        return _rteModelviewMat;
    }
    
    const cglib::mat4x4<float>& ViewState::getRTEModelviewProjectionMat() const {
        return _rteModelviewProjectionMat;
    }
        
    const cglib::mat4x4<float>& ViewState::getRTESkyProjectionMat() const {
        return _rteSkyProjectionMat;
    }

    const cglib::frustum3<double>& ViewState::getFrustum() const {
        return _frustum;
    }

    const cglib::frustum3<double>& ViewState::getLabelFrustum() const {
        return _labelFrustum;
    }
    
    int ViewState::getScreenWidth() const {
        return _width;
    }
        
    int ViewState::getScreenHeight() const {
        return _height;
    }
    
    void ViewState::setScreenSize(int width, int height) {
        _width = width;
        _height = height;
    
        _halfWidth = _width / 2.0f;
        _halfHeight = _height / 2.0f;
    
        if (_height <= 0) {
            _height = 1;
        }
        _aspectRatio = (float)_width / _height;
    
        _screenSizeChanged = true;
    }

    void ViewState::clampZoom(const Options& options) {
        // The terrain bound applies regardless of restricted panning, and only stops a zoom in:
        // a camera already under the clearance shell is lifted by MapRenderer, not zoomed out here.
        float maxZoom = std::min(options.getZoomRange().getMax(), std::max(getTerrainMaxZoom(), _zoom));
        if ((!options.isRestrictedPanning() && _zoom <= maxZoom) || _width <= 0 || _height <= 0) {
            return;
        }

        float zoom = GeneralUtils::Clamp(_zoom, getMinZoom(), maxZoom);

        if (zoom != getZoom() && _zoom0Distance > 0) {
            double length = _zoom0Distance / std::pow(2.0f, zoom);
            cglib::vec3<double> cameraPos = _focusPos + cglib::unit(_cameraPos - _focusPos) * length;

            setZoom(zoom);
            setCameraPos(cameraPos);

            cameraChanged();
        }
    }

    void ViewState::clampFocusPos(const Options& options) {
        bool seamlessPanning = options.isSeamlessPanning();
        bool restrictedPanning = options.isRestrictedPanning();
        RenderProjectionMode::RenderProjectionMode renderProjectionMode = options.getRenderProjectionMode();
        std::shared_ptr<ProjectionSurface> projectionSurface = options.getProjectionSurface();
        
        MapBounds mapBounds = options.getAdjustedInternalPanBounds(false);
        MapPos mapPos = projectionSurface->calculateMapPos(_focusPos);
        MapPos oldMapPos = mapPos;

        mapPos.setX(GeneralUtils::Clamp(mapPos.getX(), mapBounds.getMin().getX(), mapBounds.getMax().getX()));
        mapPos.setY(GeneralUtils::Clamp(mapPos.getY(), mapBounds.getMin().getY(), mapBounds.getMax().getY()));
        // Pan bounds clamp x and y only: the focus keeps its height, so an app can lift the viewpoint off the map plane.
        mapPos.setZ(oldMapPos.getZ());

        if (seamlessPanning && renderProjectionMode == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
            double n = std::floor((mapPos.getX() + Const::WORLD_SIZE * 0.5) / Const::WORLD_SIZE);
            if (n != 0) {
                mapPos.setX(mapPos.getX() - n * Const::WORLD_SIZE);
                setHorizontalLayerOffsetDir(-static_cast<int>(n));

                cameraChanged();
            }
        }

        if (mapPos != oldMapPos) {
            cglib::mat4x4<double> transform = projectionSurface->calculateTranslateMatrix(projectionSurface->calculatePosition(oldMapPos), projectionSurface->calculatePosition(mapPos), 1.0);
            setFocusPos(cglib::transform_point(_focusPos, transform));
            setCameraPos(cglib::transform_point(_cameraPos, transform));
            setUpVec(cglib::transform_vector(_upVec, transform));

            cameraChanged();
        }

        if (restrictedPanning && _width > 0 && _height > 0) {
            cglib::mat4x4<double> transform = cglib::mat4x4<double>::identity();

            for (int j = 0; j < 4; j++) {
                cglib::vec3<double> cameraVec = _cameraPos - _focusPos;
                cglib::vec3<double> focusPos = _focusPos;
                cglib::vec3<double> cameraPos = _focusPos + projectionSurface->calculateNormal(projectionSurface->calculateMapPos(_focusPos)) * cglib::length(cameraVec);
                cglib::vec3<double> upVec = projectionSurface->calculateVector(projectionSurface->calculateMapPos(_focusPos), MapVec(0, 1, 0));

                ViewState viewState;
                viewState._ignoreMinZoom = true;
                viewState._minZoom = _minZoom;
                viewState._projectionSurface = _projectionSurface;
                viewState.setFocusPos(focusPos);
                viewState.setCameraPos(cameraPos);
                viewState.setUpVec(upVec);
                viewState.setZoom(_zoom);
                viewState.setScreenSize(_width, _height);
                viewState.cameraChanged();
                viewState.calculateViewState(options);

                cglib::vec2<float> screenEdgePos(_width * (j / 2 == 0 ? (j % 2) : 0.5f), _height * (j / 2 != 0 ? (j % 2) : 0.5f));
                cglib::vec3<double> edgePos = viewState.screenToWorld(screenEdgePos, 0);
                if (!std::isfinite(cglib::norm(edgePos))) {
                    continue;
                }
                MapPos mapPosEdge = projectionSurface->calculateMapPos(edgePos);
                mapPosEdge.setZ(0);
                if (mapBounds.contains(mapPosEdge)) {
                    continue;
                }

                MapPos mapPosCenter = mapPosEdge;
                if (j / 2 == 1) {
                    mapPosCenter.setY(calculateMapBoundsCenter(options, mapBounds).getY());
                } else {
                    mapPosCenter.setX(calculateMapBoundsCenter(options, mapBounds).getX());
                }
                cglib::vec3<double> centerPos = projectionSurface->calculatePosition(mapPosCenter);
                
                MapRange range(0, 1);
                for (int i = 0; i < 24; i++) {
                    cglib::mat4x4<double> transform = projectionSurface->calculateTranslateMatrix(edgePos, centerPos, range.getMidrange());
                    viewState.setFocusPos(cglib::transform_point(focusPos, transform));
                    viewState.setCameraPos(cglib::transform_point(cameraPos, transform));
                    viewState.setUpVec(cglib::transform_vector(upVec, transform));
                    viewState.cameraChanged();
                    viewState.calculateViewState(options);

                    cglib::vec3<double> pos = viewState.screenToWorld(screenEdgePos, 0);
                    if (!std::isfinite(cglib::norm(pos))) {
                        range.setMax(range.getMidrange());
                    } else {
                        MapPos mapPos = projectionSurface->calculateMapPos(pos);
                        mapPos.setZ(0);
                        if (mapBounds.contains(mapPos)) {
                            range.setMax(range.getMidrange());
                        } else {
                            range.setMin(range.getMidrange());
                        }
                    }
                }

                transform = transform * projectionSurface->calculateTranslateMatrix(edgePos, centerPos, range.getMidrange());
            }

            if (transform != cglib::mat4x4<double>::identity()) {
                setFocusPos(cglib::transform_point(_focusPos, transform));
                setCameraPos(cglib::transform_point(_cameraPos, transform));
                setUpVec(cglib::transform_vector(_upVec, transform));

                cameraChanged();
            }
        }
    }

    cglib::vec3<float> ViewState::getFocusPosNormal() const {
        if (!_projectionSurface) {
            return cglib::vec3<float>(0, 0, 1);
        }
        return cglib::vec3<float>::convert(_projectionSurface->calculateNormal(_projectionSurface->calculateMapPos(_focusPos)));
    }

    bool ViewState::isSkyVisible() const {
        return _skyVisible;
    }

    float ViewState::getSkyHorizonNDC() const {
        return _skyHorizonNDC;
    }
    
    void ViewState::calculateViewState(const Options& options) {
        std::shared_ptr<ProjectionSurface> projectionSurface = options.getProjectionSurface();
        float FOVY = options.getFieldOfViewY();
        int tileDrawSize = options.getTileDrawSize();
        float zoomOffset = options.getZoomOffset();
        float dpi = options.getDPI();
        MapRange zoomRange = options.getZoomRange();
        bool restrictedPanning = options.isRestrictedPanning();
        if (projectionSurface != _projectionSurface || FOVY != _fovY || tileDrawSize != _tileDrawSize || zoomOffset != _zoomOffset || dpi != _dpi || zoomRange != _zoomRange || restrictedPanning != _restrictedPanning || _screenSizeChanged) {
            _fovY = FOVY;
            _tileDrawSize = tileDrawSize;
            _zoomOffset = zoomOffset;
            _dpToPX = dpi / Const::UNSCALED_DPI;
            _dpi = dpi;
            _screenSizeChanged = false;

            _halfFOVY = _fovY * 0.5f;
            _tanHalfFOVY = std::tan(static_cast<double>(_halfFOVY * Const::DEG_TO_RAD));
            _cosHalfFOVY = std::cos(static_cast<double>(_halfFOVY * Const::DEG_TO_RAD));

            _tanHalfFOVX = _aspectRatio * _tanHalfFOVY;
            _cosHalfFOVXY = std::cos(std::atan(_tanHalfFOVX)) * _cosHalfFOVY;

            _zoom0Distance = static_cast<float>(calculateZoom0Distance(_tanHalfFOVY, projectionSurface));
            _minZoom = zoomRange.getMin();
            _zoomRange = zoomRange;
            _restrictedPanning = restrictedPanning;

            // tileDrawSize, not the zoom-offset tile size: style widths and text sizes are absolute, and following
            // the offset would draw every label and line twice as wide.
            _normalizedResolution = 2 * tileDrawSize * (_dpi / Const::UNSCALED_DPI);

            if (_projectionSurface != projectionSurface) {
                MapPos focusPosInternal(0, 0, 0);
                if (_projectionSurface) {
                    focusPosInternal = _projectionSurface->calculateMapPos(_focusPos);
                }

                double sin = std::sin(-_rotation * Const::DEG_TO_RAD);
                double cos = std::cos(-_rotation * Const::DEG_TO_RAD);

                _focusPos = projectionSurface->calculatePosition(focusPosInternal);
                _cameraPos = _focusPos + projectionSurface->calculateNormal(focusPosInternal);
                _upVec = cglib::unit(projectionSurface->calculateVector(focusPosInternal, MapVec(sin, cos, 0)));

                cglib::vec3<double> axis = cglib::vector_product(_focusPos - _cameraPos, _upVec);
                if (cglib::length(axis) != 0) {
                    cglib::mat4x4<double> transform = cglib::rotate4_matrix(axis, (90 - _cameraTilt) * Const::DEG_TO_RAD);
                    _cameraPos = _focusPos + cglib::transform_vector(_cameraPos - _focusPos, transform);
                    _upVec = cglib::transform_vector(_upVec, transform);
                }

                _projectionSurface = projectionSurface;
            }

            if (_zoom0Distance > 0) {
                double length = _zoom0Distance / std::pow(2.0f, _zoom);
                _cameraPos = _focusPos + cglib::unit(_cameraPos - _focusPos) * length;
            }

            if (!_ignoreMinZoom) {
                _minZoom = calculateMinZoom(options);
            }

            _cameraChanged = true;
        }

        // On a globe the calibration follows the focus (localWorldPerInternal), so it changes as the map pans in latitude.
        if (_projectionSurface && _zoom0Distance > 0) {
            double zoom0Distance = calculateZoom0Distance(_tanHalfFOVY, _projectionSurface);
            if (zoom0Distance > 0 && std::abs(zoom0Distance - _zoom0Distance) > _zoom0Distance * 1.0e-4) {
                _zoom0Distance = static_cast<float>(zoom0Distance);
                double length = _zoom0Distance / std::pow(2.0, static_cast<double>(_zoom));
                _cameraPos = _focusPos + cglib::unit(_cameraPos - _focusPos) * length;
                _cameraChanged = true;
            }
        }

        if (_cameraChanged) {
            _cameraChanged = false;

            _unitToPXCoef = static_cast<float>(ZoomConvention::unitToPixel(_zoom0Distance, _height, _tanHalfFOVY, _2PowZoom));
            _unitToDPCoef = _unitToPXCoef * _dpi / Const::UNSCALED_DPI;

            calculateViewDistances(options, _near, _far, _skyVisible, _skyHorizonNDC);

            _projectionMat = calculatePerspMat(_halfFOVY, _near, _far, options);
            _modelviewMat = calculateLookatMat();

            cglib::mat4x4<double> invCameraMatrix = cglib::inverse(_modelviewMat);
            _rotationState.xAxis = cglib::vec3<float>::convert(cglib::proj_o(cglib::col_vector(invCameraMatrix, 0)));
            _rotationState.yAxis = cglib::vec3<float>::convert(cglib::proj_o(cglib::col_vector(invCameraMatrix, 1)));

            _modelviewProjectionMat = _projectionMat * _modelviewMat;
            _frustum = cglib::gl_projection_frustum(_modelviewProjectionMat);

            // Labels are placed in a band past the viewport, so the tiles filling that band are culled in too.
            float labelPadding = vt::ViewState::calculateLabelPadding(_tilt, options.getLabelPadding());
            cglib::mat4x4<double> labelProjectionMat = vt::ViewState::paddedProjectionMatrix(_projectionMat, labelPadding, getAspectRatio(), _normalizedResolution);
            _labelFrustum = cglib::gl_projection_frustum(labelProjectionMat * _modelviewMat);

            // The RTE modelview matrix only requires float precision.
            _rteModelviewMat = cglib::mat4x4<float>::convert(_modelviewMat);
            _rteModelviewMat(0, 3) = 0.0f;
            _rteModelviewMat(1, 3) = 0.0f;
            _rteModelviewMat(2, 3) = 0.0f;

            _rteModelviewProjectionMat = cglib::mat4x4<float>::convert(_projectionMat) * _rteModelviewMat;

            float skyFar = _zoom0Distance * options.getDrawDistance();
            cglib::mat4x4<double> skyProjectionMat = calculatePerspMat(_halfFOVY, _near, skyFar, options);
            _rteSkyProjectionMat = cglib::mat4x4<float>::convert(skyProjectionMat) * _rteModelviewMat;
        }
    }
    
    cglib::vec3<double> ViewState::screenToWorld(const cglib::vec2<float>& screenPos, double height, std::shared_ptr<Options> options) const {
        if (_width <= 0 || _height <= 0) {
            Log::Error("ViewState::screenToWorld: Failed to transform point from screen space to world plane, screen size is unknown");
            return cglib::vec3<double>(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
        }

        std::shared_ptr<ProjectionSurface> projectionSurface = _projectionSurface;
        cglib::mat4x4<double> modelviewProjectionMat = _modelviewProjectionMat;
        if (options) {
            projectionSurface = options->getProjectionSurface();
            modelviewProjectionMat = calculateModelViewMat(*options);
        }
        if (!projectionSurface) {
            return cglib::vec3<double>(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
        }
        cglib::mat4x4<double> invModelviewProjectionMat = cglib::inverse(modelviewProjectionMat);

        cglib::vec3<double> screenPos0(screenPos(0) / _width * 2 - 1, 1 - screenPos(1) / _height * 2, -1);
        cglib::vec3<double> screenPos1(screenPos(0) / _width * 2 - 1, 1 - screenPos(1) / _height * 2,  1);
        cglib::vec3<double> worldPos0 = cglib::transform_point(screenPos0, invModelviewProjectionMat);
        cglib::vec3<double> worldPos1 = cglib::transform_point(screenPos1, invModelviewProjectionMat);
        cglib::ray3<double> ray(worldPos0, worldPos1 - worldPos0);

        double t = -1;
        if (!projectionSurface->calculateHitPoint(ray, height, t) || t < 0) {
            return cglib::vec3<double>(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
        }
        return ray(t);
    }
    
    cglib::vec2<float> ViewState::worldToScreen(const cglib::vec3<double>& worldPos, std::shared_ptr<Options> options) const {
        if (_width <= 0 || _height <= 0) {
            Log::Error("ViewState::worldToScreen: Failed to transform point from world to screen space, screen size is unknown");
            return cglib::vec2<float>(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
        }
        
        cglib::mat4x4<double> modelviewProjectionMat = _modelviewProjectionMat;
        if (options) {
            modelviewProjectionMat = calculateModelViewMat(*options);
        }

        cglib::vec3<float> screenPos = cglib::vec3<float>::convert(cglib::transform_point(worldPos, modelviewProjectionMat));
        return cglib::vec2<float>((screenPos(0) + 1) * 0.5f * _width, (1 - screenPos(1)) * 0.5f * _height);
    }

    float ViewState::estimateWorldPixelMeasure() const {
        if (_width <= 0 || _height <= 0) {
            Log::Error("ViewState::estimateWorldPixelMeasure: Failed to estimate pixel size, screen size is unknown");
            return 0;
        }
        if (!_projectionSurface) {
            return 0;
        }

        cglib::mat4x4<double> invModelviewProjectionMat = cglib::inverse(_modelviewProjectionMat);

        cglib::vec3<double> worldPos = cglib::vec3<double>::zero();
        for (int iter = -1; iter < 8; iter++) {
            double dx = (iter < 0 ? 0 : std::pow(2.0f, -iter));
            cglib::vec3<double> screenPos0((_width * 0.5f + dx) / _width * 2 - 1, 1 - (_height * 0.5f) / _height * 2, -1);
            cglib::vec3<double> screenPos1((_width * 0.5f + dx) / _width * 2 - 1, 1 - (_height * 0.5f) / _height * 2,  1);
            cglib::vec3<double> worldPos0 = cglib::transform_point(screenPos0, invModelviewProjectionMat);
            cglib::vec3<double> worldPos1 = cglib::transform_point(screenPos1, invModelviewProjectionMat);
            cglib::ray3<double> ray(worldPos0, worldPos1 - worldPos0);

            double t = -1;
            if (_projectionSurface->calculateHitPoint(ray, 0, t) && t >= 0) {
                if (iter >= 0) {
                    return static_cast<float>(_projectionSurface->calculateDistance(ray(t), worldPos) / dx);
                }
                worldPos = ray(t);
            } else if (iter < 0) {
                break;
            }
        }
        return 0;
    }
    
    int ViewState::getHorizontalLayerOffsetDir() const {
        return _horizontalLayerOffsetDir;
    }
    
    void ViewState::setHorizontalLayerOffsetDir(int horizontalLayerOffsetDir) {
        _horizontalLayerOffsetDir = horizontalLayerOffsetDir;
    }

    void ViewState::calculateViewDistances(const Options& options, float& near, float& far, bool& skyVisible) const {
        float horizonNDC = 1.0f;
        calculateViewDistances(options, near, far, skyVisible, horizonNDC);
    }

    void ViewState::calculateViewDistances(const Options& options, float& near, float& far, bool& skyVisible, float& skyHorizonNDC) const {
        float halfFOVY = options.getFieldOfViewY() * 0.5f;
        float tanHalfFOVY = std::tan(static_cast<float>(halfFOVY * Const::DEG_TO_RAD));
        float zoom0Distance = static_cast<float>(calculateZoom0Distance(tanHalfFOVY, _projectionSurface));
        float initialZ = std::pow(2.0f, -_zoom) * zoom0Distance / 64.0f;
        // Above the horizon the camera does not look at the focus point (calculateLookatMat).
        cglib::vec3<double> zProjVector = calculateViewDir();

        cglib::mat4x4<double> projMat = calculatePerspMat(halfFOVY, initialZ, 2.0f * initialZ, options);
        cglib::mat4x4<double> modelviewMat = calculateLookatMat();
        cglib::mat4x4<double> invModelviewProjMat = cglib::inverse(projMat * modelviewMat);

        double heightMin = std::min(static_cast<double>(Const::MIN_HEIGHT), static_cast<double>(_terrainHeightMin));
        double heightMax = std::max(static_cast<double>(Const::MAX_HEIGHT), static_cast<double>(_terrainHeightMax));

        near = static_cast<float>(cglib::dot_product(options.getProjectionSurface()->calculateNearestPoint(_cameraPos, heightMax) - _cameraPos, zProjVector));
        far  = near;
        skyVisible = false;
        bool groundVisible = false;
        // The first ray of each bisected column that reaches no ground is the horizon; the sky quad is clipped
        // to the lowest one, the way tangram's sky mesh is.
        skyHorizonNDC = 1.0f;
        // Bisected from a point that sees ground: the centre, unless the camera looks up (#285), where a
        // centre in the sky clipped the quad at the middle row and left a black band below.
        bool centreOnGround = false;
        {
            cglib::vec3<double> worldPos0 = cglib::transform_point(cglib::vec3<double>(0, 0, -1), invModelviewProjMat);
            cglib::vec3<double> worldPos1 = cglib::transform_point(cglib::vec3<double>(0, 0,  1), invModelviewProjMat);
            double t = -1;
            centreOnGround = options.getProjectionSurface()->calculateHitPoint(cglib::ray3<double>(worldPos0, worldPos1 - worldPos0), heightMin, t) && t > 0;
        }
        for (double xx : { -1, 0, 1 }) {
            for (double yy : { -1, 0, 1 }) {
                double x0 = centreOnGround ? 0 : xx, y0 = centreOnGround ? 0 : -1, x1 = xx, y1 = yy;
                for (int iter = -1; iter < 16; iter++) {
                    double x = (iter < 0 ? xx : (x0 + x1) * 0.5), y = (iter < 0 ? yy : (y0 + y1) * 0.5);
                    cglib::vec3<double> worldPos0 = cglib::transform_point(cglib::vec3<double>(x, y, -1), invModelviewProjMat);
                    cglib::vec3<double> worldPos1 = cglib::transform_point(cglib::vec3<double>(x, y,  1), invModelviewProjMat);
                    cglib::ray3<double> ray(worldPos0, worldPos1 - worldPos0);

                    double t = -1;
                    if (options.getProjectionSurface()->calculateHitPoint(ray, heightMin, t) && t > 0) {
                        float z = static_cast<float>(cglib::dot_product(ray(t) - worldPos0, zProjVector));
                        near = std::min(near, z);
                        far  = std::max(far,  z);
                        groundVisible = true;

                        if (iter < 0) {
                            break;
                        }
                        
                        x0 = x; y0 = y;
                    } else {
                        skyVisible = true;
                        skyHorizonNDC = std::min(skyHorizonNDC, static_cast<float>(y));

                        x1 = x; y1 = y;
                    }
                }
            }
        }

        // Not the orbit alone: that cut off peaks in front of a low camera. See 05-depth-model.md.
        double orbitDistance = std::pow(2.0f, -_zoom) * zoom0Distance;
        double cameraHeight = ViewDistance::cameraHeight(orbitDistance, _cameraPos(2));
        double maxDist = ViewDistance::drawCeiling(cameraHeight, options.getDrawDistance(), Const::WORLD_SIZE * std::pow(2.0, -_zoom),
                                                   std::sin(_tilt * Const::DEG_TO_RAD));
        double rayFar = far;
        if (far > maxDist) {
            far = maxDist;
            skyVisible = true;
            // Ground cut off by the draw distance rather than the horizon: the sky can reach anywhere, no clip.
            skyHorizonNDC = -1.0f;
        }

        near = std::max(Const::MIN_NEAR, near) * 0.8f;
        far  = std::max(Const::MIN_NEAR, far)  * 1.1f;

        double viewDistance = calculateViewDistance(options);
        // Floor near at distance / 50 (tangram's `m_pos.z / 50`): the nearest ground reaches centimetres on a slope (04-terrain.md).
        // Our camera is held above the ground under it, not off the terrain, so take the smaller distance.
        double cameraDistance = calculateCameraDistance();
        if (_terrainCameraZ != 0 && _cameraPos(2) > _terrainCameraZ) {
            cameraDistance = std::min(cameraDistance, _cameraPos(2) - _terrainCameraZ);
        }
        float terrainNear = static_cast<float>(cameraDistance / 50.0);
        if (viewDistance > 0) {
            float viewDistanceFactor = 1.0f;
            bool absoluteViewDistance = false;
            double maxDistance = 0;
            if (std::shared_ptr<TerrainOptions> terrainOptions = options.getTerrainOptions()) {
                viewDistanceFactor = terrainOptions->getViewDistanceFactor();
                // Only when the absolute distance won over the rule and is not under the ceiling.
                double absolute = terrainOptions->getViewDistance() * static_cast<double>(Const::WORLD_SIZE) / Const::EARTH_CIRCUMFERENCE;
                maxDistance = terrainOptions->getViewDistanceMax() * static_cast<double>(Const::WORLD_SIZE) / Const::EARTH_CIRCUMFERENCE;
                absoluteViewDistance = absolute > 0 && absolute >= viewDistance && !(maxDistance > 0 && maxDistance < absolute);
            }
            if (absoluteViewDistance || viewDistanceFactor > 1.0f) {
                // More ground than tangram's rule: the far plane follows or the extra tiles are fetched and clipped,
                // at a depth-precision cost the app opted into.
                far = std::max(far, static_cast<float>(viewDistance));
            } else {
                far = std::min(far, std::max(static_cast<float>(viewDistance), terrainNear * 2.0f));
            }
            // The ceiling caps the far plane whichever branch ran.
            if (maxDistance > 0) {
                far = std::min(far, std::max(static_cast<float>(maxDistance), terrainNear * 2.0f));
            }
        }
        if (_terrainHeightMax > _terrainHeightMin) {
            near = std::max(near, std::min(terrainNear, far * 0.5f));
        }
        if (_cameraTilt != _tilt) {
            // Above the horizon the ground hits walk off into the distance, pulling the near plane out with them.
            near = std::min(near, terrainNear);
        }
        if (!groundVisible) {
            // Nothing but sky left far == near: give the celestial objects parked inside far a real depth range.
            near = std::max(near, terrainNear);
            far = std::max(static_cast<float>(viewDistance > 0 ? viewDistance : maxDist), near * 2.0f);
        }
        logViewDistances(options, near, far, rayFar, maxDist, viewDistance, cameraHeight);
    }

    // Which of the four limits ended the map, in km: 05-depth-model.md. Atomic limiter: the cull worker calls it too.
    void ViewState::logViewDistances(const Options& options, float near, float far, double rayFar, double maxDist, double viewDistance, double cameraHeight) const {
#if MASSIF_FRAME_PROFILER
        // One limiter per tag, or one ViewState starves the other's log.
        static std::atomic<long long> lastLogMs[2] = { { 0 }, { 0 } };
        int logSlot = (_terrainHeightMax > _terrainHeightMin ? 0 : 1);
        long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        long long last = lastLogMs[logSlot].load();
        if (nowMs - last < 1000 || !lastLogMs[logSlot].compare_exchange_strong(last, nowMs)) {
            return;
        }
        double toKm = Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE / 1000.0;
        float fogStart = 0, fogEnd = 0;
        if (std::shared_ptr<FogOptions> fogOptions = options.getFogOptions()) {
            fogStart = static_cast<float>(fogOptions->getRangeStart() * calculateCameraDistance() * toKm);
            fogEnd = static_cast<float>(fogOptions->getRangeEnd() * calculateCameraDistance() * toKm);
        }
        // Tagged: the cull worker's copy never gets setTerrainHeightRange, so its near plane is a flat world's.
        Log::Infof("PROF VIEW[%s]: zoom %.2f tilt %.1f | orbit %.2f alt %.2f height %.2f km | terrain %.2f..%.2f km | ray far %.2f ceiling %.2f rule %.2f -> near %.4f far %.2f km | fog %.2f..%.2f km",
            (_terrainHeightMax > _terrainHeightMin ? "render" : "no-terrain-range"),
            _zoom, _tilt, calculateCameraDistance() * toKm, _cameraPos(2) * toKm, cameraHeight * toKm,
            _terrainHeightMin * toKm, _terrainHeightMax * toKm,
            rayFar * toKm, maxDist * toKm, viewDistance * toKm, near * toKm, far * toKm, fogStart, fogEnd);
#endif
    }

    double ViewState::calculateCameraDistance() const {
        // Tangram's m_pos.z: a function of zoom alone, so the depth budget does not depend on the terrain.
        return cglib::length(_cameraPos - _focusPos);
    }

    double ViewState::calculateViewDistance(const Options& options) const {
        // Tangram's rule (view.cpp): 2*m_pos.z / cos(pitch + fovy/2), capped at 127 tile widths for near-horizontal views.
        // The factor scales it: 1 is their rule, 0 the ground-derived one.
        float factor = 1.0f;
        double absoluteDistance = 0;
        // The app's own ceiling. Not derived from the fog: ground above the haze must still be drawn.
        double maxDistance = 0;
        if (std::shared_ptr<TerrainOptions> terrainOptions = options.getTerrainOptions()) {
            absoluteDistance = terrainOptions->getViewDistance() * static_cast<double>(Const::WORLD_SIZE) / Const::EARTH_CIRCUMFERENCE;
            maxDistance = terrainOptions->getViewDistanceMax() * static_cast<double>(Const::WORLD_SIZE) / Const::EARTH_CIRCUMFERENCE;
            factor = terrainOptions->getViewDistanceFactor();
        }
        if (!(factor > 0.0f)) {
            return maxDistance > 0 ? std::min(absoluteDistance, maxDistance) : absoluteDistance;
        }
        // With 3D terrain the height above ground and the focus distance differ; take the larger (a summit needs the height).
        double cameraDistance = ViewDistance::cameraHeight(calculateCameraDistance(), _cameraPos(2));

        // Tilt is measured from the horizontal here and pitch from the vertical there, so the
        // angle from the view axis to the horizon is (90 - tilt) + fovy/2.
        double pitchRadians = (90.0 - _tilt) * Const::DEG_TO_RAD + _halfFOVY * Const::DEG_TO_RAD;
        double cosPitch = std::cos(pitchRadians);
        double distance = std::numeric_limits<double>::infinity();
        if (cosPitch > 0.0) {
            distance = TANGRAM_FAR_PLANE_FACTOR * cameraDistance / cosPitch;
        }
        distance = std::min(distance, ViewDistance::tileWalkCap(Const::WORLD_SIZE * std::pow(2.0, -_zoom)));
        // An absolute distance only extends the rule: metres winning outright end the ground inside a zoomed-out screen (#156).
        double viewDistance = std::max(distance * factor, absoluteDistance);
        // Applied last, so it caps the minimum too.
        return maxDistance > 0 ? std::min(viewDistance, maxDistance) : viewDistance;
    }
    
    float ViewState::calculateMinZoom(const Options& options) const {
        if (!options.isRestrictedPanning() || _width <= 0 || _height <= 0) {
            return options.getZoomRange().getMin();
        }

        std::shared_ptr<ProjectionSurface> projectionSurface = options.getProjectionSurface();

        MapBounds mapBounds = options.getAdjustedInternalPanBounds(false);
        MapPos mapPos = calculateMapBoundsCenter(options, mapBounds);

        MapRange range = options.getZoomRange();
        for (int i = 0; i < 24; i++) {
            cglib::vec3<double> cameraVec = _cameraPos - _focusPos;
            cglib::vec3<double> focusPos = projectionSurface->calculatePosition(mapPos);
            cglib::vec3<double> cameraPos = focusPos + projectionSurface->calculateNormal(mapPos) * cglib::length(cameraVec);
            cglib::vec3<double> upVec = projectionSurface->calculateVector(mapPos, MapVec(0, 1, 0));

            ViewState viewState;
            viewState._ignoreMinZoom = true;
            viewState._projectionSurface = _projectionSurface;
            viewState.setFocusPos(focusPos);
            viewState.setCameraPos(cameraPos);
            viewState.setUpVec(upVec);
            viewState.setZoom(range.getMidrange());
            viewState.setScreenSize(_width, _height);
            viewState.cameraChanged();
            viewState.calculateViewState(options);

            bool fit = true;
            for (int j = 0; j < 4; j++) {
                cglib::vec2<float> screenEdgePos(_width * (j / 2 == 0 ? (j % 2) : 0.5f), _height * (j / 2 != 0 ? (j % 2) : 0.5f));
                cglib::vec3<double> edgePos = viewState.screenToWorld(screenEdgePos, 0);
                if (!std::isfinite(cglib::norm(edgePos))) {
                    fit = false;
                    break;
                }
                MapPos mapPosEdge = projectionSurface->calculateMapPos(edgePos);
                mapPosEdge.setZ(0); // important in spherical mode due to small imprecisions in conversions
                if (!mapBounds.contains(mapPosEdge)) {
                    fit = false;
                    break;
                }
            }

            if (fit) {
                range.setMax(viewState.getZoom());
            } else {
                range.setMin(viewState.getZoom());
            }
        }

        return range.getMidrange();
    }

    MapPos ViewState::calculateMapBoundsCenter(const Options& options, const MapBounds& mapBounds) const {
        MapPos centerPos(0, 0);
        for (int i = 0; i < 2; i++) {
            if (mapBounds.getMin()[i] != -std::numeric_limits<double>::infinity() || mapBounds.getMax()[i] != std::numeric_limits<double>::infinity()) {
                centerPos[i] = mapBounds.getMin()[i] * 0.5 + mapBounds.getMax()[i] * 0.5;
            }
        }
        return centerPos;
    }
    
    cglib::mat4x4<double> ViewState::calculatePerspMat(float halfFOVY, float near, float far, const Options& options) const {
        double tanHalfFOVY = std::tan(halfFOVY * Const::DEG_TO_RAD);
        double top = near * tanHalfFOVY;
        double bottom = -top;
        double left = bottom * _aspectRatio;
        double right = top * _aspectRatio;

        double dx =  2 * near * tanHalfFOVY * options.getFocusPointOffset().getX() / _height;
        double dy = -2 * near * tanHalfFOVY * options.getFocusPointOffset().getY() / _height;
        
        top += dy;
        bottom += dy;
        left += dx;
        right += dx;
        
        return cglib::frustum4_matrix(left, right, bottom, top, static_cast<double>(near), static_cast<double>(far));
    }
    
    cglib::mat4x4<double> ViewState::calculateLookatMat() const {
        float viewPitch = _cameraTilt - _tilt;
        if (viewPitch == 0) {
            return cglib::lookat4_matrix(_cameraPos, _focusPos, _upVec);
        }
        // Pitch the view about the camera, not the focus: that would put the camera under the ground,
        // and this keeps dist(camera, focus), so zoom and culling are untouched.
        cglib::vec3<double> viewVec = _focusPos - _cameraPos;
        cglib::vec3<double> axis = cglib::vector_product(viewVec, _upVec);
        if (cglib::length(axis) == 0) {
            return cglib::lookat4_matrix(_cameraPos, _focusPos, _upVec);
        }
        cglib::mat4x4<double> transform = cglib::rotate4_matrix(axis, viewPitch * Const::DEG_TO_RAD);
        return cglib::lookat4_matrix(_cameraPos, _cameraPos + cglib::transform_vector(viewVec, transform), cglib::transform_vector(_upVec, transform));
    }
    
    cglib::mat4x4<double> ViewState::calculateModelViewMat(const massif::Options& options) const {
        if (_cameraChanged) {
            // The render thread has not updated the matrices for this camera yet.
            float near = 0;
            float far = 0;
            bool skyVisible = false;
            calculateViewDistances(options, near, far, skyVisible);
            
            cglib::mat4x4<double> projectionMat = calculatePerspMat(options.getFieldOfViewY() * 0.5f, near, far, options);
            cglib::mat4x4<double> modelviewMat = calculateLookatMat();
            return projectionMat * modelviewMat;
        }
        
        return _modelviewProjectionMat;
    }
}

