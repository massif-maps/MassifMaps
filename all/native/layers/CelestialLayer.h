/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALLAYER_H_
#define _MASSIF_CELESTIALLAYER_H_

#include "components/DirectorPtr.h"
#include "layers/Layer.h"

#include <memory>
#include <vector>

namespace massif {
    class CelestialObject;
    class CelestialRenderer;
    class CelestialEventListener;

    /**
     * A layer of objects placed in the sky by direction rather than on the map (unless one asks, see CelestialObject::setPosition).
     * Added first in the layer order, every object is behind the map and terrain hides what is behind a ridge.
     * Objects are batched by bitmap, so thousands sharing one bitmap (or none) cost a single draw call.
     */
    class CelestialLayer : public Layer {
    public:
        CelestialLayer();
        virtual ~CelestialLayer();

        /**
         * Adds an object to the layer.
         * @param object The object to add.
         */
        void add(const std::shared_ptr<CelestialObject>& object);
        /**
         * Adds a list of objects to the layer. Cheaper than adding them one at a time, which
         * rebuilds the batches per object.
         * @param objects The objects to add.
         */
        void addAll(const std::vector<std::shared_ptr<CelestialObject> >& objects);
        /**
         * Removes an object from the layer.
         * @param object The object to remove.
         * @return True if the object was found and removed.
         */
        bool remove(const std::shared_ptr<CelestialObject>& object);
        /**
         * Removes every object from the layer.
         */
        void clear();
        /**
         * Returns all objects of the layer.
         * @return The objects of the layer.
         */
        std::vector<std::shared_ptr<CelestialObject> > getAll() const;

        /**
         * Returns the object event listener.
         * @return The object event listener.
         */
        std::shared_ptr<CelestialEventListener> getCelestialEventListener() const;
        /**
         * Sets the object event listener, which reports clicks on objects of this layer.
         * @param listener The new object event listener.
         */
        void setCelestialEventListener(const std::shared_ptr<CelestialEventListener>& listener);

        virtual bool isUpdateInProgress() const;

    protected:
        virtual void setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                   const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                   const std::weak_ptr<Options>& options,
                                   const std::weak_ptr<MapRenderer>& mapRenderer,
                                   const std::weak_ptr<TouchHandler>& touchHandler);

        virtual void loadData(const std::shared_ptr<CullState>& cullState);

        virtual void offsetLayerHorizontally(double offset);

        virtual bool onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState);
        virtual bool onDrawFrame3D(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState);

        virtual void calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const;
        virtual bool processClick(const ClickInfo& clickInfo, const RayIntersectedElement& intersectedElement, const ViewState& viewState) const;

        virtual void registerDataSourceListener();
        virtual void unregisterDataSourceListener();

    private:
        void refreshRenderer();

        std::vector<std::shared_ptr<CelestialObject> > _objects;
        std::shared_ptr<CelestialRenderer> _celestialRenderer;
        ThreadSafeDirectorPtr<CelestialEventListener> _celestialEventListener;
    };

}

#endif
