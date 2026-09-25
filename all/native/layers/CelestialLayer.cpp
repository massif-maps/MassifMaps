#include "CelestialLayer.h"
#include "celestial/CelestialObject.h"
#include "components/Exceptions.h"
#include "layers/CelestialEventListener.h"
#include "renderers/CelestialRenderer.h"
#include "renderers/MapRenderer.h"
#include "renderers/components/RayIntersectedElement.h"
#include "ui/ClickInfo.h"
#include "utils/Log.h"

#include <algorithm>

namespace massif {

    CelestialLayer::CelestialLayer() :
        Layer(),
        _objects(),
        _celestialRenderer(std::make_shared<CelestialRenderer>()),
        _celestialEventListener()
    {
    }

    CelestialLayer::~CelestialLayer() {
    }

    void CelestialLayer::add(const std::shared_ptr<CelestialObject>& object) {
        if (!object) {
            throw NullArgumentException("Null object");
        }
        addAll(std::vector<std::shared_ptr<CelestialObject> > { object });
    }

    void CelestialLayer::addAll(const std::vector<std::shared_ptr<CelestialObject> >& objects) {
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            for (const std::shared_ptr<CelestialObject>& object : objects) {
                if (!object) {
                    throw NullArgumentException("Null object");
                }
                _objects.push_back(object);
            }
        }
        for (const std::shared_ptr<CelestialObject>& object : objects) {
            object->setComponents(std::static_pointer_cast<CelestialLayer>(shared_from_this()));
        }
        refreshRenderer();
    }

    bool CelestialLayer::remove(const std::shared_ptr<CelestialObject>& object) {
        bool removed = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            auto it = std::find(_objects.begin(), _objects.end(), object);
            if (it != _objects.end()) {
                _objects.erase(it);
                removed = true;
            }
        }
        if (removed) {
            object->setComponents(std::shared_ptr<CelestialLayer>());
            refreshRenderer();
        }
        return removed;
    }

    void CelestialLayer::clear() {
        std::vector<std::shared_ptr<CelestialObject> > objects;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            std::swap(objects, _objects);
        }
        for (const std::shared_ptr<CelestialObject>& object : objects) {
            object->setComponents(std::shared_ptr<CelestialLayer>());
        }
        refreshRenderer();
    }

    std::vector<std::shared_ptr<CelestialObject> > CelestialLayer::getAll() const {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _objects;
    }

    std::shared_ptr<CelestialEventListener> CelestialLayer::getCelestialEventListener() const {
        return _celestialEventListener.get();
    }

    void CelestialLayer::setCelestialEventListener(const std::shared_ptr<CelestialEventListener>& listener) {
        _celestialEventListener.set(listener);
    }

    bool CelestialLayer::isUpdateInProgress() const {
        return false;
    }

    void CelestialLayer::setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                       const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                       const std::weak_ptr<Options>& options,
                                       const std::weak_ptr<MapRenderer>& mapRenderer,
                                       const std::weak_ptr<TouchHandler>& touchHandler)
    {
        Layer::setComponents(envelopeThreadPool, tileThreadPool, options, mapRenderer, touchHandler);
        _celestialRenderer->setComponents(options, mapRenderer);
        refreshRenderer();
    }

    void CelestialLayer::loadData(const std::shared_ptr<CullState>& cullState) {
        // Nothing to load: the objects are held by the layer itself, and where they end up on
        // screen is decided per frame from the camera, not from the visible tile set.
    }

    void CelestialLayer::offsetLayerHorizontally(double offset) {
        // Direction-anchored objects have no horizontal position to offset, and position-anchored
        // ones are resolved through the projection surface every frame.
    }

    bool CelestialLayer::onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState) {
        return false;
    }

    // Drawn in the 3D pass, where the vector layers draw their labels: the LAYER ORDER then decides
    // what is on top, so a celestial layer above a labelled one writes over its labels and one below
    // it goes under them. In the base pass every label covered every sky object, whatever the order.
    // The depth test against the map is the same in either pass.
    bool CelestialLayer::onDrawFrame3D(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState) {
        if (!isVisible() || !getVisibleZoomRange().inRange(viewState.getZoom()) || getOpacity() <= 0) {
            return false;
        }
        return _celestialRenderer->onDrawFrame(deltaSeconds, getOpacity(), viewState);
    }

    void CelestialLayer::calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
        std::shared_ptr<CelestialLayer> thisLayer = std::const_pointer_cast<CelestialLayer>(std::static_pointer_cast<const CelestialLayer>(shared_from_this()));
        _celestialRenderer->calculateRayIntersectedElements(thisLayer, ray, viewState, results);
    }

    bool CelestialLayer::processClick(const ClickInfo& clickInfo, const RayIntersectedElement& intersectedElement, const ViewState& viewState) const {
        DirectorPtr<CelestialEventListener> listener = _celestialEventListener;
        if (!listener) {
            return clickInfo.getClickType() == ClickType::CLICK_TYPE_SINGLE;
        }
        std::shared_ptr<CelestialObject> object = intersectedElement.getElement<CelestialObject>();
        return listener->onCelestialObjectClicked(clickInfo, object);
    }

    void CelestialLayer::registerDataSourceListener() {
    }

    void CelestialLayer::unregisterDataSourceListener() {
    }

    void CelestialLayer::refreshRenderer() {
        std::vector<std::shared_ptr<CelestialObject> > objects = getAll();
        _celestialRenderer->refreshObjects(objects);
        redraw();
    }

}
