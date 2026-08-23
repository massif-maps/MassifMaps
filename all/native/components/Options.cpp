#include "Options.h"
#include "assets/DefaultBackgroundPNG.h"
#include "components/Exceptions.h"
#include "components/CancelableThreadPool.h"
#include "graphics/Bitmap.h"
#include "graphics/utils/SkyBitmapGenerator.h"
#include "projections/EPSG3857.h"
#include "projections/ProjectionSurface.h"
#include "projections/PlanarProjectionSurface.h"
#include "projections/SphericalProjectionSurface.h"
#include "utils/Const.h"
#include "utils/Log.h"
#include "utils/GeneralUtils.h"

#include <algorithm>

namespace massif {

    Options::Options(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool, const std::shared_ptr<CancelableThreadPool>& tileThreadPool) :
        _ambientLightColor(DEFAULT_AMBIENT_LIGHT_COLOR),
        _mainLightColor(DEFAULT_MAIN_LIGHT_COLOR),
        _mainLightDir(DEFAULT_MAIN_LIGHT_DIR),
        _renderProjectionMode(RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR),
        _debugTileBorders(false),
        _clickTypeDetection(true),
        _doubleClickDetection(true),
        _longClickDuration(DEFAULT_LONG_CLICK_DURATION),
        _doubleClickMaxDuration(DEFAULT_DOUBLE_CLICK_MAX_DURATION),
        _tileDrawSize(256),
        // 0.5, not tangram's 1.0: half a nominal tile of screen area before the next zoom level is
        // used. This is the value every bench and every example screenshot in this repo was made
        // with - see scripts/android-dev/.../demo/DemoConfig.java, which is where the tuning was
        // done, and docs/internals/rendering/02-tiles-lod.md.
        _tileLODFactor(0.5f),
        _tileLODForeshorteningLimit(0.0f),
        _dpi(160.0f),
        _drawDistance(16),
        _fovY(70),
        _panningMode(PanningMode::PANNING_MODE_FREE),
        _pivotMode(PivotMode::PIVOT_MODE_TOUCHPOINT),
        _seamlessPanning(true),
        _restrictedPanning(false),
        _tiltGestureReversed(false),
        // ON: double-tap to zoom in, two-finger tap to zoom out and double-tap-and-drag to zoom
        // continuously. Every other map SDK does these by default, and an app that had to ask for
        // them just looked broken.
        _zoomGestures(true),
        _rotationGestures(true),
        _clearColor(DEFAULT_CLEAR_COLOR),
        _skyColor(DEFAULT_SKY_COLOR),
        _skyBitmapColor(0, 0, 0, 0),
        _skyBitmap(),
        _backgroundBitmap(GetDefaultBackgroundBitmap()),
        _userInput(true),
        _panningSpeedMode(PanningSpeedMode::PANNING_SPEED_MODE_ANCHORED),
        _freeRoamMode(FreeRoamMode::FREE_ROAM_MODE_OFF),
        _freeRoamLookSensitivity(90.0f),
        _freeRoamMoveSpeed(0.5f),
        _kineticPan(true),
        _kineticRotation(true),
        _kineticZoom(true),
        _rotatable(true),
        _tiltRange(3.0f, 90.0f), // not MIN_SUPPORTED_TILT_ANGLE: looking above the horizon is opt-in
        _zoomRange(0.0, Const::MAX_SUPPORTED_ZOOM_LEVEL),
        _panBounds(MapPos(-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()), MapPos(std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity())),
        _focusPointOffset(0, 0),
        _baseProjection(std::make_shared<EPSG3857>()),
        _projectionSurface(std::make_shared<PlanarProjectionSurface>()),
        _envelopeThreadPool(envelopeThreadPool),
        _tileThreadPool(tileThreadPool),
        _layersLabelsProcessedInReverseOrder(true),
        _mutex()
    {
        setEnvelopeThreadPoolSize(1);
        // 2 decode threads, as tangram runs (SceneOptions::numTileWorkers). One is what made tiles
        // arrive late enough to be visible.
        setTileThreadPoolSize(2);
    }
    
    Options::~Options() {
        if (_terrainOptions && _terrainOptionsListener) {
            _terrainOptions->unregisterOnChangeListener(_terrainOptionsListener);
        }
        if (_skyOptions && _skyOptionsListener) {
            _skyOptions->unregisterOnChangeListener(_skyOptionsListener);
        }
        if (_fogOptions && _fogOptionsListener) {
            _fogOptions->unregisterOnChangeListener(_fogOptionsListener);
        }
        if (_lightOptions && _lightOptionsListener) {
            _lightOptions->unregisterOnChangeListener(_lightOptionsListener);
        }
    }
        
    Color Options::getAmbientLightColor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _ambientLightColor;
    }
    
    void Options::setAmbientLightColor(const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_ambientLightColor == color) {
                return;
            }
            _ambientLightColor = color;
        }
        notifyOptionChanged("AmbientLightColor");
    }
    
    Color Options::getMainLightColor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _mainLightColor;
    }
    
    void Options::setMainLightColor(const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_mainLightColor == color) {
                return;
            }
            _mainLightColor = color;
        }
        notifyOptionChanged("MainLightColor");
    }
    
    MapVec Options::getMainLightDirection() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _mainLightDir;
    }
    
    void Options::setMainLightDirection(const MapVec& direction) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            MapVec directionNormalized = direction;
            directionNormalized.normalize();
            if (_mainLightDir == directionNormalized) {
                return;
            }
            _mainLightDir = directionNormalized;
        }
        notifyOptionChanged("MainLightDirection");
    }
    
    RenderProjectionMode::RenderProjectionMode Options::getRenderProjectionMode() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _renderProjectionMode;
    }
    
    void Options::setRenderProjectionMode(RenderProjectionMode::RenderProjectionMode renderProjectionMode) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_renderProjectionMode == renderProjectionMode) {
                return;
            }
            _renderProjectionMode = renderProjectionMode;
            switch (renderProjectionMode) {
            case RenderProjectionMode::RENDER_PROJECTION_MODE_SPHERICAL:
                _projectionSurface = std::make_shared<SphericalProjectionSurface>();
                break;
            case RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR:
            default:
                _projectionSurface = std::make_shared<PlanarProjectionSurface>();
                break;
            }
        }
        notifyOptionChanged("RenderProjectionMode");
    }
    
    bool Options::isDebugTileBorders() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _debugTileBorders;
    }

    void Options::setDebugTileBorders(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_debugTileBorders == enabled) {
                return;
            }
            _debugTileBorders = enabled;
        }
        notifyOptionChanged("DebugTileBorders");
    }

    bool Options::isClickTypeDetection() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _clickTypeDetection;
    }
    
    void Options::setClickTypeDetection(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_clickTypeDetection == enabled) {
                return;
            }
            _clickTypeDetection = enabled;
        }
        notifyOptionChanged("ClickTypeDetection");
    }
    
    bool Options::isDoubleClickDetection() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _doubleClickDetection;
    }
    
    void Options::setDoubleClickDetection(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_doubleClickDetection == enabled) {
                return;
            }
            _doubleClickDetection = enabled;
        }
        notifyOptionChanged("DoubleClickDetection");
    }

    float Options::getLongClickDuration() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _longClickDuration;
    }

    void Options::setLongClickDuration(float duration) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_longClickDuration == duration) {
                return;
            }
            _longClickDuration = duration;
        }
        notifyOptionChanged("LongClickDuration");
    }

    float Options::getDoubleClickMaxDuration() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _doubleClickMaxDuration;
    }

    void Options::setDoubleClickMaxDuration(float duration) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_doubleClickMaxDuration == duration) {
                return;
            }
            _doubleClickMaxDuration = duration;
        }
        notifyOptionChanged("DoubleClickMaxDuration");
    }

    int Options::getTileDrawSize() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tileDrawSize;
    }
    
    void Options::setTileDrawSize(int tileDrawSize) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_tileDrawSize == tileDrawSize) {
                return;
            }
            _tileDrawSize = tileDrawSize;
        }
        notifyOptionChanged("TileDrawSize");
    }

    float Options::getTileLODFactor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tileLODFactor;
    }

    void Options::setTileLODFactor(float factor) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            float clamped = std::max(0.0f, factor);
            if (_tileLODFactor == clamped) {
                return;
            }
            _tileLODFactor = clamped;
        }
        notifyOptionChanged("TileLODFactor");
    }

    float Options::getTileLODForeshorteningLimit() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tileLODForeshorteningLimit;
    }

    void Options::setTileLODForeshorteningLimit(float levels) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            float clamped = std::max(0.0f, levels);
            if (_tileLODForeshorteningLimit == clamped) {
                return;
            }
            _tileLODForeshorteningLimit = clamped;
        }
        notifyOptionChanged("TileLODForeshorteningLimit");
    }
    
    float Options::getDPI() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _dpi;
    }
    
    void Options::setDPI(float dpi) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_dpi == dpi) {
                return;
            }
            _dpi = dpi;
        }
        notifyOptionChanged("DPI");
    }
    
    float Options::getDrawDistance() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _drawDistance;
    }
    
    void Options::setDrawDistance(float drawDistance) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_drawDistance == drawDistance) {
                return;
            }
            _drawDistance = drawDistance;
        }
        notifyOptionChanged("DrawDistance");
    }
    
    int Options::getFieldOfViewY() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _fovY;
    }
    
    void Options::setFieldOfViewY(int fovY) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_fovY == fovY) {
                return;
            }
            _fovY = fovY;
        }
        notifyOptionChanged("FieldOfViewY");
    }
    
    PanningMode::PanningMode Options::getPanningMode() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _panningMode;
    }
    
    void Options::setPanningMode(PanningMode::PanningMode panningMode) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_panningMode == panningMode) {
                return;
            }
            _panningMode = panningMode;
        }
        notifyOptionChanged("PanningMode");
    }
    
    PivotMode::PivotMode Options::getPivotMode() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _pivotMode;
    }

    void Options::setPivotMode(PivotMode::PivotMode pivotMode) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_pivotMode == pivotMode) {
                return;
            }
            _pivotMode = pivotMode;
        }
        notifyOptionChanged("PivotMode");
    }

    bool Options::isSeamlessPanning() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _seamlessPanning;
    }
    
    void Options::setSeamlessPanning(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_seamlessPanning == enabled) {
                return;
            }
            _seamlessPanning = enabled;
        }
        notifyOptionChanged("SeamlessPanning");
    }
        
    bool Options::isRestrictedPanning() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _restrictedPanning;
    }
    
    void Options::setRestrictedPanning(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_restrictedPanning == enabled) {
                return;
            }
            _restrictedPanning = enabled;
        }
        notifyOptionChanged("RestrictedPanning");
    }
        
    bool Options::isTiltGestureReversed() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tiltGestureReversed;
    }
    
    void Options::setTiltGestureReversed(bool reversed) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_tiltGestureReversed == reversed) {
                return;
            }
            _tiltGestureReversed = reversed;
        }
        notifyOptionChanged("TiltGestureReversed");
    }
        
    bool Options::isZoomGestures() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _zoomGestures;
    }
    
    void Options::setZoomGestures(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_zoomGestures == enabled) {
                return;
            }
            _zoomGestures = enabled;
        }
        notifyOptionChanged("ZoomGestures");
    }

    bool Options::isRotationGestures() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _rotationGestures;
    }
    
    void Options::setRotationGestures(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_rotationGestures == enabled) {
                return;
            }
            _rotationGestures = enabled;
        }
        notifyOptionChanged("RotationGestures");
    }
        
    int Options::getEnvelopeThreadPoolSize() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _envelopeThreadPool->getPoolSize();
    }
    
    void Options::setEnvelopeThreadPoolSize(int poolSize) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_envelopeThreadPool->getPoolSize() == poolSize) {
                return;
            }
            _envelopeThreadPool->setPoolSize(poolSize);
        }
        notifyOptionChanged("EnvelopeThreadPoolSize");
    }
    
    int Options::getTileThreadPoolSize() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tileThreadPool->getPoolSize();
    }
    
    void Options::setTileThreadPoolSize(int poolSize) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_tileThreadPool->getPoolSize() == poolSize) {
                return;
            }
            _tileThreadPool->setPoolSize(poolSize);
        }
        notifyOptionChanged("TileThreadPoolSize");
    }
    
    Color Options::getClearColor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _clearColor;
    }

    void Options::setClearColor(const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_clearColor == color) {
                return;
            }
            _clearColor = color;
        }
        notifyOptionChanged("ClearColor");
    }
    
    Color Options::getSkyColor() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _skyColor;
    }

    void Options::setSkyColor(const Color& color) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_skyColor == color) {
                return;
            }
            _skyColor = color;
        }
        notifyOptionChanged("SkyColor");
    }

    std::shared_ptr<Bitmap> Options::getSkyBitmap() const {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_skyBitmapColor != _skyColor) {
            if (_skyColor != Color(0, 0, 0, 0)) {
                _skyBitmap = SkyBitmapGenerator(1, 128).generateBitmap(DEFAULT_BACKGROUND_COLOR, _skyColor);
            } else {
                _skyBitmap.reset();
            }
            _skyBitmapColor = _skyColor;
        }
        return _skyBitmap;
    }

    std::shared_ptr<Bitmap> Options::getBackgroundBitmap() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _backgroundBitmap;
    }

    void Options::setBackgroundBitmap(const std::shared_ptr<Bitmap>& backgroundBitmap) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_backgroundBitmap == backgroundBitmap) {
                return;
            }
            _backgroundBitmap = backgroundBitmap;
        }
        notifyOptionChanged("BackgroundBitmap");
    }
    
    bool Options::isUserInput() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _userInput;
    }
    
    void Options::setUserInput(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_userInput == enabled) {
                return;
            }
            _userInput = enabled;
        }
        notifyOptionChanged("UserInput");
    }
    
    PanningSpeedMode::PanningSpeedMode Options::getPanningSpeedMode() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _panningSpeedMode;
    }

    void Options::setPanningSpeedMode(PanningSpeedMode::PanningSpeedMode mode) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_panningSpeedMode == mode) {
                return;
            }
            _panningSpeedMode = mode;
        }
        notifyOptionChanged("PanningSpeedMode");
    }

    FreeRoamMode::FreeRoamMode Options::getFreeRoamMode() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _freeRoamMode;
    }

    void Options::setFreeRoamMode(FreeRoamMode::FreeRoamMode mode) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_freeRoamMode == mode) {
                return;
            }
            _freeRoamMode = mode;
        }
        notifyOptionChanged("FreeRoamMode");
    }

    float Options::getFreeRoamLookSensitivity() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _freeRoamLookSensitivity;
    }

    void Options::setFreeRoamLookSensitivity(float degreesPerInch) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_freeRoamLookSensitivity == degreesPerInch) {
                return;
            }
            _freeRoamLookSensitivity = degreesPerInch;
        }
        notifyOptionChanged("FreeRoamLookSensitivity");
    }

    float Options::getFreeRoamMoveSpeed() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _freeRoamMoveSpeed;
    }

    void Options::setFreeRoamMoveSpeed(float distancePerInch) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_freeRoamMoveSpeed == distancePerInch) {
                return;
            }
            _freeRoamMoveSpeed = distancePerInch;
        }
        notifyOptionChanged("FreeRoamMoveSpeed");
    }

    bool Options::isKineticPan() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _kineticPan;
    }
    
    void Options::setKineticPan(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_kineticPan == enabled) {
                return;
            }
            _kineticPan = enabled;
        }
        notifyOptionChanged("KineticPan");
    }
    
    bool Options::isKineticRotation() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _kineticRotation;
    }
    
    void Options::setKineticRotation(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_kineticRotation == enabled) {
                return;
            }
            _kineticRotation = enabled;
        }
        notifyOptionChanged("KineticRotation");
    }
        
    bool Options::isKineticZoom() {
        std::lock_guard<std::mutex> lock(_mutex);
        return _kineticZoom;
    }
    
    void Options::setKineticZoom(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_kineticZoom == enabled) {
                return;
            }
            _kineticZoom = enabled;
        }
        notifyOptionChanged("KineticZoom");
    }
    
    bool Options::isRotatable() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _rotatable;
    }
    
    void Options::setRotatable(bool rotatable) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_rotatable == rotatable) {
                return;
            }
            _rotatable = rotatable;
        }
        notifyOptionChanged("Rotatable");
    }
    
    MapRange Options::getTiltRange() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tiltRange;
    }
    
    void Options::setTiltRange(const MapRange& tiltRange) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            float min = GeneralUtils::Clamp(tiltRange.getMin(), Const::MIN_SUPPORTED_TILT_ANGLE, 90.0f);
            float max = GeneralUtils::Clamp(tiltRange.getMax(), Const::MIN_SUPPORTED_TILT_ANGLE, 90.0f);
            MapRange tiltRangeClipped(min, max);
            if (tiltRangeClipped.getMin() > tiltRangeClipped.getMax()) {
                throw InvalidArgumentException("Min larger than max in tiltRange");
            }
            if (_tiltRange == tiltRangeClipped) {
                return;
            }
            _tiltRange = tiltRangeClipped;
        }
        notifyOptionChanged("TiltRange");
    }
    
    MapRange Options::getZoomRange() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _zoomRange;
    }
    
    void Options::setZoomRange(const MapRange& zoomRange) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            float min = GeneralUtils::Clamp(zoomRange.getMin(), 0.0f, static_cast<float>(Const::MAX_SUPPORTED_ZOOM_LEVEL));
            float max = GeneralUtils::Clamp(zoomRange.getMax(), 0.0f, static_cast<float>(Const::MAX_SUPPORTED_ZOOM_LEVEL));
            MapRange zoomRangeClipped(min, max);
            if (zoomRangeClipped.getMin() > zoomRangeClipped.getMax()) {
                throw InvalidArgumentException("Min larger than max in zoomRange");
            }
            if (_zoomRange == zoomRangeClipped) {
                return;
            }
            _zoomRange = zoomRangeClipped;
        }
        notifyOptionChanged("ZoomRange");
    }
        
    MapBounds Options::getPanBounds() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _panBounds;
    }
    
    void Options::setPanBounds(const MapBounds& panBounds) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (panBounds.getMin().getX() > panBounds.getMax().getX() || panBounds.getMin().getY() > panBounds.getMax().getY()) {
                throw InvalidArgumentException("Min larger than max in panBounds");
            }
            if (_panBounds == panBounds) {
                return;
            }
            _panBounds = panBounds;
        }
        notifyOptionChanged("PanBounds");
    }
    
    MapBounds Options::getAdjustedInternalPanBounds(bool clamp) const {
        std::lock_guard<std::mutex> lock(_mutex);

        MapBounds panBoundsClipped = _panBounds;
        panBoundsClipped.shrinkToIntersection(_baseProjection->getBounds());

        MapPos panBoundsMin = _panBounds.getMin();
        MapPos panBoundsMax = _panBounds.getMax();
        if (std::isfinite(panBoundsMin.getX())) {
            panBoundsMin.setX(_baseProjection->toInternal(panBoundsClipped.getMin()).getX());
        }
        if (std::isfinite(panBoundsMin.getY())) {
            panBoundsMin.setY(_baseProjection->toInternal(panBoundsClipped.getMin()).getY());
        }
        if (std::isfinite(panBoundsMax.getX())) {
            panBoundsMax.setX(_baseProjection->toInternal(panBoundsClipped.getMax()).getX());
        }
        if (std::isfinite(panBoundsMax.getY())) {
            panBoundsMax.setY(_baseProjection->toInternal(panBoundsClipped.getMax()).getY());
        }

        if (_renderProjectionMode == RenderProjectionMode::RENDER_PROJECTION_MODE_PLANAR) {
            EPSG3857 proj;
            MapPos projBoundsMin = proj.toInternal(proj.getBounds().getMin());
            MapPos projBoundsMax = proj.toInternal(proj.getBounds().getMax());

            panBoundsMin.setY(std::max(panBoundsMin.getY(), projBoundsMin.getY()));
            panBoundsMax.setY(std::min(panBoundsMax.getY(), projBoundsMax.getY()));
            if (!_seamlessPanning) {
                panBoundsMin.setX(std::max(panBoundsMin.getX(), projBoundsMin.getX()));
                panBoundsMax.setX(std::min(panBoundsMax.getX(), projBoundsMax.getX()));
            }
        }

        if (clamp) {
            panBoundsMin.setX(GeneralUtils::Clamp(panBoundsMin.getX(), -Const::WORLD_SIZE * 0.5, Const::WORLD_SIZE * 0.5));
            panBoundsMin.setY(GeneralUtils::Clamp(panBoundsMin.getY(), -Const::WORLD_SIZE * 0.5, Const::WORLD_SIZE * 0.5));
            panBoundsMax.setX(GeneralUtils::Clamp(panBoundsMax.getX(), -Const::WORLD_SIZE * 0.5, Const::WORLD_SIZE * 0.5));
            panBoundsMax.setY(GeneralUtils::Clamp(panBoundsMax.getY(), -Const::WORLD_SIZE * 0.5, Const::WORLD_SIZE * 0.5));
        }
        return MapBounds(panBoundsMin, panBoundsMax);
    }
    
    ScreenPos Options::getFocusPointOffset() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _focusPointOffset;
    }
    
    void Options::setFocusPointOffset(const ScreenPos& offset) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_focusPointOffset == offset) {
                return;
            }
            _focusPointOffset = offset;
        }
        notifyOptionChanged("FocusPointOffset");
    }
    
    std::shared_ptr<Projection> Options::getBaseProjection() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _baseProjection;
    }
    
    void Options::setBaseProjection(const std::shared_ptr<Projection>& baseProjection) {
        if (!baseProjection) {
            throw NullArgumentException("Null baseProjection");
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_baseProjection == baseProjection) {
                return;
            }
            _baseProjection = baseProjection;
        }
        notifyOptionChanged("BaseProjection");
    }
    
    std::shared_ptr<ProjectionSurface> Options::getProjectionSurface() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _projectionSurface;
    }

    std::shared_ptr<TerrainOptions> Options::getTerrainOptions() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _terrainOptions;
    }

    void Options::setTerrainOptions(const std::shared_ptr<TerrainOptions>& terrainOptions) {
        struct TerrainOptionsListener : TerrainOptions::OnChangeListener {
            explicit TerrainOptionsListener(Options& options) : _options(options) { }

            virtual void onTerrainOptionChanged(const std::string& optionName) override {
                _options.notifyOptionChanged("TerrainOptions." + optionName);
            }

        private:
            Options& _options;
        };

        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_terrainOptions == terrainOptions) {
                return;
            }
            if (_terrainOptions && _terrainOptionsListener) {
                _terrainOptions->unregisterOnChangeListener(_terrainOptionsListener);
                _terrainOptionsListener.reset();
            }
            _terrainOptions = terrainOptions;
            if (_terrainOptions) {
                _terrainOptionsListener = std::make_shared<TerrainOptionsListener>(*this);
                _terrainOptions->registerOnChangeListener(_terrainOptionsListener);
            }
        }
        notifyOptionChanged("TerrainOptions");
    }

    std::shared_ptr<SkyOptions> Options::getSkyOptions() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _skyOptions;
    }

    void Options::setSkyOptions(const std::shared_ptr<SkyOptions>& skyOptions) {
        struct SkyOptionsListener : SkyOptions::OnChangeListener {
            explicit SkyOptionsListener(Options& options) : _options(options) { }

            virtual void onSkyOptionChanged(const std::string& optionName) override {
                _options.notifyOptionChanged("SkyOptions." + optionName);
            }

        private:
            Options& _options;
        };

        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_skyOptions == skyOptions) {
                return;
            }
            if (_skyOptions && _skyOptionsListener) {
                _skyOptions->unregisterOnChangeListener(_skyOptionsListener);
                _skyOptionsListener.reset();
            }
            _skyOptions = skyOptions;
            if (_skyOptions) {
                _skyOptionsListener = std::make_shared<SkyOptionsListener>(*this);
                _skyOptions->registerOnChangeListener(_skyOptionsListener);
            }
        }
        notifyOptionChanged("SkyOptions");
    }

    std::shared_ptr<FogOptions> Options::getFogOptions() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _fogOptions;
    }

    void Options::setFogOptions(const std::shared_ptr<FogOptions>& fogOptions) {
        struct FogOptionsListener : FogOptions::OnChangeListener {
            explicit FogOptionsListener(Options& options) : _options(options) { }

            virtual void onFogOptionChanged(const std::string& optionName) override {
                _options.notifyOptionChanged("FogOptions." + optionName);
            }

        private:
            Options& _options;
        };

        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_fogOptions == fogOptions) {
                return;
            }
            if (_fogOptions && _fogOptionsListener) {
                _fogOptions->unregisterOnChangeListener(_fogOptionsListener);
                _fogOptionsListener.reset();
            }
            _fogOptions = fogOptions;
            if (_fogOptions) {
                _fogOptionsListener = std::make_shared<FogOptionsListener>(*this);
                _fogOptions->registerOnChangeListener(_fogOptionsListener);
            }
        }
        notifyOptionChanged("FogOptions");
    }

    std::shared_ptr<LightOptions> Options::getLightOptions() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lightOptions;
    }

    void Options::setLightOptions(const std::shared_ptr<LightOptions>& lightOptions) {
        struct LightOptionsListener : LightOptions::OnChangeListener {
            explicit LightOptionsListener(Options& options) : _options(options) { }

            virtual void onLightOptionChanged(const std::string& optionName) override {
                _options.notifyOptionChanged("LightOptions." + optionName);
            }

        private:
            Options& _options;
        };

        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_lightOptions == lightOptions) {
                return;
            }
            if (_lightOptions && _lightOptionsListener) {
                _lightOptions->unregisterOnChangeListener(_lightOptionsListener);
                _lightOptionsListener.reset();
            }
            _lightOptions = lightOptions;
            if (_lightOptions) {
                _lightOptionsListener = std::make_shared<LightOptionsListener>(*this);
                _lightOptions->registerOnChangeListener(_lightOptionsListener);
            }
        }
        notifyOptionChanged("LightOptions");
    }
    
    void Options::setLayersLabelsProcessedInReverseOrder(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_layersLabelsProcessedInReverseOrder == enabled) {
                return;
            }
            _layersLabelsProcessedInReverseOrder = enabled;
        }
        notifyOptionChanged("LayersLabelsProcessedInReverseOrder");
    }
    
    bool Options::isLayersLabelsProcessedInReverseOrder() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _layersLabelsProcessedInReverseOrder;
    }
    
    void Options::registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.push_back(listener);
    }

    void Options::unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener) {
        std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
        _onChangeListeners.erase(std::remove(_onChangeListeners.begin(), _onChangeListeners.end(), listener), _onChangeListeners.end());
    }
    
    std::shared_ptr<Bitmap> Options::GetDefaultBackgroundBitmap() {
        std::lock_guard<std::mutex> lock(_Mutex);
        if (!_DefaultBackgroundBitmap) {
            _DefaultBackgroundBitmap = Bitmap::CreateFromCompressed(default_background_png, default_background_png_len);
        }
        return _DefaultBackgroundBitmap;
    }
        
    void Options::notifyOptionChanged(const std::string& optionName) {
        std::vector<std::shared_ptr<OnChangeListener> > onChangeListeners;
        {
            std::lock_guard<std::mutex> lock(_onChangeListenersMutex);
            onChangeListeners = _onChangeListeners;
        }
        for (const std::shared_ptr<OnChangeListener>& listener : onChangeListeners) {
            listener->onOptionChanged(optionName);
        }
    }

    const float Options::DEFAULT_LONG_CLICK_DURATION = 0.4f;
    const float Options::DEFAULT_DOUBLE_CLICK_MAX_DURATION = 0.4f;
    const Color Options::DEFAULT_CLEAR_COLOR = Color(0, 0, 0, 255);
    const Color Options::DEFAULT_SKY_COLOR = Color(149, 196, 255, 255);
    const Color Options::DEFAULT_BACKGROUND_COLOR = Color(226, 226, 226, 255);
    const Color Options::DEFAULT_AMBIENT_LIGHT_COLOR = Color(112, 112, 112, 255);
    const Color Options::DEFAULT_MAIN_LIGHT_COLOR = Color(143, 143, 143, 255);
    const MapVec Options::DEFAULT_MAIN_LIGHT_DIR = MapVec(0.35, 0.35, -0.87);

    std::shared_ptr<Bitmap> Options::_DefaultBackgroundBitmap;
    
    std::mutex Options::_Mutex;
    
}
