/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_CUSTOMLAYER_H_
#define _MASSIF_API_CUSTOMLAYER_H_

#include "layers/Layer.h"

#include <memory>
#include <string>

namespace massif {
    class GLResource;
    class GLResourceManager;
    class ViewState;
}

namespace massif { namespace api {
    class CustomLayerHost;

    /** A layer drawn by the caller's mm_custom_layer (mm_layer_create_custom). */
    class CustomLayer : public Layer {
    public:
        explicit CustomLayer(const std::shared_ptr<CustomLayerHost>& host);
        virtual ~CustomLayer();

        virtual bool isUpdateInProgress() const;

        void requestRedraw() const;

    protected:
        virtual void setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                   const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                   const std::weak_ptr<Options>& options,
                                   const std::weak_ptr<MapRenderer>& mapRenderer,
                                   const std::weak_ptr<TouchHandler>& touchHandler);

        virtual void loadData(const std::shared_ptr<CullState>& cullState);

        virtual void offsetLayerHorizontally(double offset);

        virtual bool onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState);
        virtual bool onDrawOverlay(float deltaSeconds, const ViewState& viewState);

        virtual void calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const;
        virtual bool processClick(const ClickInfo& clickInfo, const RayIntersectedElement& intersectedElement, const ViewState& viewState) const;

        virtual void registerDataSourceListener();
        virtual void unregisterDataSourceListener();

    private:
        const std::shared_ptr<CustomLayerHost> _host;
        // Dropping it queues the caller's on_removed for the GL thread; a new manager means a new context.
        std::shared_ptr<GLResource> _glResource;
        std::weak_ptr<GLResourceManager> _glManager;
        // Debug builds name a state leak once, not every frame.
        std::string _reportedLeak;
    };

} }

#endif
