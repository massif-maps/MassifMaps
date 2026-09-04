#include "ViewState.h"
#include "projections/EPSG3857.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "terrain/CameraClearance.h"
#include "utils/Const.h"
#include "utils/GeneralUtils.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cglib/mat.h>

namespace massif {

    // Tangram's constants (core/src/view/view.cpp): the far-plane factor on the camera height,
    // and the LOD depth whose 2^(d+1)-1 = 127 tile widths cap how far tiles are ever walked.
    static const double TANGRAM_FAR_PLANE_FACTOR = 2.0;
    static const int MAX_TILE_LOD = 6;

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

    void ViewState::setTerrainCameraReference(double terrainZ, double clearanceFloor) {
        if (terrainZ != _terrainCameraZ) {
            _terrainCameraZ = terrainZ;
            _cameraChanged = true; // the near plane is built on it
        }
        _terrainClearanceFloor = clearanceFloor;
        _terrainCameraBound = true;
    }

    void ViewState::clearTerrainCameraReference() {
        if (_terrainCameraZ != 0) {
            _terrainCameraZ = 0;
            _cameraChanged = true;
        }
        _terrainClearanceFloor = 0;
        _terrainCameraBound = false;
    }

    float ViewState::getTerrainMaxZoom() const {
        // Zooming scales the camera-to-FOCUS vector (clampZoom below rebuilds the camera position
        // exactly that way), so what shrinks as 1/2^zoom is the camera's height above the focus,
        // not its height above z=0; the focus sits on the ground, hundreds or thousands of metres
        // up. CameraClearance::maxZoom solves on that vector, and answers infinity for a camera
        // that no zoom can raise (at or below the focus height).
        if (!_terrainCameraBound || !(_zoom0Distance > 0)) {
            return std::numeric_limits<float>::infinity();
        }
        return CameraClearance::maxZoom(_zoom, _focusPos(2), _cameraPos(2), getOrbitDistance(_zoom), _terrainCameraZ,
                                        getOrbitDistance(_zoomRange.getMax()), _terrainClearanceFloor);
    }

    double ViewState::getOrbitDistance(float zoom) const {
        return _zoom0Distance / std::pow(2.0, static_cast<double>(zoom));
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
        // The map camera model: the camera sits at the tilt, and everything below the horizon is
        // a rotation of the view about it. setViewTilt is the other model, where the camera stays.
        _cameraTilt = std::max(tilt, 0.0f);
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
    
    int ViewState::getFOVY() const {
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
        // The terrain bound applies regardless of restricted panning - it is a property of
        // the terrain, not of the pan bounds. It only ever STOPS a zoom in: a camera already
        // under the clearance shell is lifted by MapRenderer, not zoomed out from here.
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
        // The pan bounds are a GROUND rectangle: they clamp x and y. The focus keeps its height,
        // so an application can lift the viewpoint off the map plane (setFocusPos with a z) - a
        // panorama seen from higher up than the ground under it. Zeroing it here pulled the focus,
        // and with it the camera, back down on every frame.
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
        // If FOV or tile draw size changed, recalculate zoom0Distance
        std::shared_ptr<ProjectionSurface> projectionSurface = options.getProjectionSurface();
        int FOVY = options.getFieldOfViewY();
        int tileDrawSize = options.getTileDrawSize();
        float dpi = options.getDPI();
        MapRange zoomRange = options.getZoomRange();
        bool restrictedPanning = options.isRestrictedPanning();
        if (projectionSurface != _projectionSurface || FOVY != _fovY || tileDrawSize != _tileDrawSize || dpi != _dpi || zoomRange != _zoomRange || restrictedPanning != _restrictedPanning || _screenSizeChanged) {
            _fovY = FOVY;
            _tileDrawSize = tileDrawSize;
            _dpToPX = dpi / Const::UNSCALED_DPI;
            _dpi = dpi;
            _screenSizeChanged = false;

            _halfFOVY = _fovY * 0.5f;
            _tanHalfFOVY = std::tan(static_cast<double>(_halfFOVY * Const::DEG_TO_RAD));
            _cosHalfFOVY = std::cos(static_cast<double>(_halfFOVY * Const::DEG_TO_RAD));

            _tanHalfFOVX = _aspectRatio * _tanHalfFOVY;
            _cosHalfFOVXY = std::cos(std::atan(_tanHalfFOVX)) * _cosHalfFOVY;

            _zoom0Distance = static_cast<float>(_height * 0.5 * Const::WORLD_SIZE / (tileDrawSize * _tanHalfFOVY * (_dpi / Const::UNSCALED_DPI)));
            _minZoom = zoomRange.getMin();
            _zoomRange = zoomRange;
            _restrictedPanning = restrictedPanning;

            _normalizedResolution = 2 * tileDrawSize * (_dpi / Const::UNSCALED_DPI);

            // Recalculate camera orientation on projection change
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

            // Calculate new camera position
            if (_zoom0Distance > 0) {
                double length = _zoom0Distance / std::pow(2.0f, _zoom);
                _cameraPos = _focusPos + cglib::unit(_cameraPos - _focusPos) * length;
            }

            // Calculate min zoom
            if (!_ignoreMinZoom) {
                _minZoom = calculateMinZoom(options);
            }

            _cameraChanged = true;
        }

        if (_cameraChanged) {
            _cameraChanged = false;

            // Calculate scaling factor for vector elements
            _unitToPXCoef = static_cast<float>(_zoom0Distance / (_height * _tanHalfFOVY) / _2PowZoom);
            _unitToDPCoef = _unitToPXCoef * _dpi / Const::UNSCALED_DPI;

            calculateViewDistances(options, _near, _far, _skyVisible, _skyHorizonNDC);

            // Matrices
            _projectionMat = calculatePerspMat(_halfFOVY, _near, _far, options);
            _modelviewMat = calculateLookatMat();

            // Rotation state
            cglib::mat4x4<double> invCameraMatrix = cglib::inverse(_modelviewMat);
            _rotationState.xAxis = cglib::vec3<float>::convert(cglib::proj_o(cglib::col_vector(invCameraMatrix, 0)));
            _rotationState.yAxis = cglib::vec3<float>::convert(cglib::proj_o(cglib::col_vector(invCameraMatrix, 1)));

            // Double precision mvp matrix and frustum
            _modelviewProjectionMat = _projectionMat * _modelviewMat;
            _frustum = cglib::gl_projection_frustum(_modelviewProjectionMat);

            // Rte modleview matrix only requires float precision
            _rteModelviewMat = cglib::mat4x4<float>::convert(_modelviewMat);
            _rteModelviewMat(0, 3) = 0.0f;
            _rteModelviewMat(1, 3) = 0.0f;
            _rteModelviewMat(2, 3) = 0.0f;

            // Float precision Rte mvp matrix
            _rteModelviewProjectionMat = cglib::mat4x4<float>::convert(_projectionMat) * _rteModelviewMat;

            // Calculate Rte sky matrix
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

        // Transform 2 points with different z values from world to screen
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

        // Transfrom world pos to screen
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

        // Try consecutive horizontal points
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
        float zoom0Distance = _height * 0.5 * Const::WORLD_SIZE / (_tileDrawSize * tanHalfFOVY * (_dpi / Const::UNSCALED_DPI));
        float initialZ = std::pow(2.0f, -_zoom) * zoom0Distance / 64.0f;
        // The direction the camera actually looks along, which above the horizon is not the
        // direction of the focus point (calculateLookatMat).
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
        // ... and where the sky starts on screen. The bisection below already walks each column
        // from the ground up to the first ray that reaches no ground at all, which IS the horizon;
        // the lowest such sample over the columns is where the sky can first appear. The sky quad
        // is clipped to it instead of covering the screen (SkyRenderer), the way tangram's sky
        // mesh is (core/src/util/skyManager.cpp).
        skyHorizonNDC = 1.0f;
        for (double xx : { -1, 0, 1 }) {
            for (double yy : { -1, 0, 1 }) {
                double x0 = 0, y0 = 0, x1 = xx, y1 = yy;
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

        double maxDist = std::pow(2.0f, -_zoom) * zoom0Distance * options.getDrawDistance();
        if (far > maxDist) {
            far = maxDist;
            skyVisible = true;
            // The ground was cut off by the draw distance rather than by the horizon, so the sky
            // can reach anywhere the ground no longer does: no clip.
            skyHorizonNDC = -1.0f;
        }

        near = std::max(Const::MIN_NEAR, near) * 0.8f;
        far  = std::max(Const::MIN_NEAR, far)  * 1.1f;

        // Floor the near plane at camera height / 50, tangram's `near = m_pos.z / 50.0`: taken from
        // the nearest visible ground it reaches centimetres against a slope, and that far/near ratio
        // is the mechanism behind every see-through. docs/internals/rendering/04-terrain.md.
        double viewDistance = calculateViewDistance(options);
        // Tangram's near is a fiftieth of the camera's distance to what it looks at, and their
        // camera is held a distance away from the TERRAIN (view.cpp: the depth at the screen
        // centre against minCameraDist). Ours is held a clearance above the terrain UNDER it, so at
        // a low tilt the focus is kilometres away while the ground is a couple of hundred metres
        // below - a fiftieth of the focus distance then parks the near plane in front of the ground
        // at the bottom of the screen and cuts it away. Take the smaller of the two distances: over
        // flat ground with the focus close they are the same, and it is only the close-to-terrain
        // case that spends depth precision.
        double cameraDistance = calculateCameraDistance();
        if (_terrainCameraZ != 0 && _cameraPos(2) > _terrainCameraZ) {
            cameraDistance = std::min(cameraDistance, _cameraPos(2) - _terrainCameraZ);
        }
        float terrainNear = static_cast<float>(cameraDistance / 50.0);
        if (viewDistance > 0) {
            float viewDistanceFactor = 1.0f;
            bool absoluteViewDistance = false;
            if (std::shared_ptr<TerrainOptions> terrainOptions = options.getTerrainOptions()) {
                viewDistanceFactor = terrainOptions->getViewDistanceFactor();
                // Only when the absolute distance is the one that WON: it merely extends the rule
                // now, and where the rule is longer this is the plain factor case.
                double absolute = terrainOptions->getViewDistance() * static_cast<double>(Const::WORLD_SIZE) / Const::EARTH_CIRCUMFERENCE;
                absoluteViewDistance = absolute > 0 && absolute >= viewDistance;
            }
            if (absoluteViewDistance || viewDistanceFactor > 1.0f) {
                // The app has asked for MORE ground than tangram's rule gives. The far plane has
                // to follow, or the extra tiles the walk fetches are drawn and then clipped - which
                // is what "raising the view distance does nothing" was. It costs depth precision:
                // the whole depth model is calibrated on the far/near ratio (see below), so this is
                // an explicit trade, not the default.
                far = std::max(far, static_cast<float>(viewDistance));
            } else {
                far = std::min(far, std::max(static_cast<float>(viewDistance), terrainNear * 2.0f));
            }
        }
        if (_terrainHeightMax > _terrainHeightMin) {
            near = std::max(near, std::min(terrainNear, far * 0.5f));
        }
        if (_cameraTilt != _tilt) {
            // The view is pitched away from the camera geometry - looking above the horizon, or a
            // first person camera. The loop above takes the near plane from where the sampled rays
            // MEET THE GROUND, and as the view pitches up those hits walk off into the distance:
            // the near plane follows them out and starts clipping everything close to the camera,
            // worse the higher the view goes. What is close to the camera does not move when the
            // view turns, so cap the near plane with the rule that does not depend on the view
            // direction at all - tangram's `near = m_pos.z / 50`.
            near = std::min(near, terrainNear);
        }
        if (!groundVisible) {
            // Nothing but sky: no ray met the ground, so the loop above left far == near and the
            // depth range would collapse onto the near plane, taking everything drawn INTO the sky
            // (celestial objects park just inside the far plane) with it. Give it the distance the
            // ground would have been drawn to.
            near = std::max(near, terrainNear);
            far = std::max(static_cast<float>(viewDistance > 0 ? viewDistance : maxDist), near * 2.0f);
        }
    }

    double ViewState::calculateCameraDistance() const {
        // Tangram's m_pos.z: the distance to the FOCUS, a function of zoom alone ("using non-zero
        // elevation for camera reference creates all kinds of problems"). The camera's height above
        // sea level instead makes the whole depth budget a function of the terrain.
        return cglib::length(_cameraPos - _focusPos);
    }

    double ViewState::calculateViewDistance(const Options& options) const {
        // Tangram's rule verbatim (view.cpp): 2*m_pos.z / cos(pitch + fovy/2), capped at 127 tile
        // widths (MAX_LOD 6). The cosine alone goes to infinity near the horizon, so the tile-count
        // cap is what bounds a near-horizontal view. The factor scales it: 1 is their rule, 0 falls
        // back to the ground-derived view distance.
        float factor = 1.0f;
        double absoluteDistance = 0;
        if (std::shared_ptr<TerrainOptions> terrainOptions = options.getTerrainOptions()) {
            absoluteDistance = terrainOptions->getViewDistance() * static_cast<double>(Const::WORLD_SIZE) / Const::EARTH_CIRCUMFERENCE;
            factor = terrainOptions->getViewDistanceFactor();
        }
        if (!(factor > 0.0f)) {
            return absoluteDistance;
        }
        // Tangram's m_pos.z is the camera's height above the ground PLANE, which for their camera
        // is also the zoom-derived distance to the focus. With 3D terrain and a free camera the two
        // part company: a viewpoint standing on a 2600 m summit is high above the ground while its
        // zoom says it is close to it, and the zoom-derived quantity alone then draws the ground
        // out to a few kilometres - the closer to the terrain, the less of the panorama. Take the
        // larger of the two, so the rule follows whichever reason there is to see far. (The NEAR
        // plane keeps the zoom-derived distance on purpose - see calculateCameraDistance.)
        double cameraDistance = std::max(calculateCameraDistance(), _cameraPos(2));

        // Tilt is measured from the horizontal here and pitch from the vertical there, so the
        // angle from the view axis to the horizon is (90 - tilt) + fovy/2.
        double pitchRadians = (90.0 - _tilt) * Const::DEG_TO_RAD + _halfFOVY * Const::DEG_TO_RAD;
        double cosPitch = std::cos(pitchRadians);
        double distance = std::numeric_limits<double>::infinity();
        if (cosPitch > 0.0) {
            distance = TANGRAM_FAR_PLANE_FACTOR * cameraDistance / cosPitch;
        }
        // 127 tile widths at this zoom, in internal units.
        double worldTileSize = Const::WORLD_SIZE * std::pow(2.0, -_zoom);
        distance = std::min(distance, worldTileSize * (std::pow(2.0, MAX_TILE_LOD + 1) - 1.0));
        // An absolute distance only ever EXTENDS the rule. Metres are zoom-independent while the
        // rule scales with 2^-zoom, so letting metres win outright ends the ground in a disc well
        // inside a zoomed-out screen (MassifMaps#156).
        return std::max(distance * factor, absoluteDistance);
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
        // Above the horizon the camera stays exactly where the tilt geometry left it and only the
        // view direction pitches up. Carrying on rotating the camera about the focus - which is
        // what tilting does between 90 and 0 degrees - puts the camera under the ground, and the
        // view comes back upside down because the up vector is derived from a focus point on the
        // ground. Rotating the view about the CAMERA has neither problem, and it keeps
        // dist(camera, focus), so zoom, culling and the near/far budget are untouched.
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
            // Camera has changed, but the matrices have not been updated yet from the render thread. Calculate far and near distances
            float near = 0;
            float far = 0;
            bool skyVisible = false;
            calculateViewDistances(options, near, far, skyVisible);
            
            // Matrices
            cglib::mat4x4<double> projectionMat = calculatePerspMat(options.getFieldOfViewY() * 0.5f, near, far, options);
            cglib::mat4x4<double> modelviewMat = calculateLookatMat();
            return projectionMat * modelviewMat;
        }
        
        // Matrices are up to date, no need to calculate a new one
        return _modelviewProjectionMat;
    }
}

