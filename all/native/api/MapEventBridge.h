/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_MAPEVENTBRIDGE_H_
#define _MASSIF_API_MAPEVENTBRIDGE_H_

#include "api/Context.h"
#include "components/DirectorPtr.h"
#include "layers/VectorElementEventListener.h"
#include "layers/VectorTileEventListener.h"
#include "ui/MapEventListener.h"

#include <memory>
#include <string>

namespace massif { namespace api {

    /**
     * Registers an event's data as a payload, emits, and drops the id again. A queued handler
     * keeps the payload alive through the normal retain.
     */
    class PayloadEmitter {
    public:
        PayloadEmitter(const std::shared_ptr<Context>& context, Handle target);

        /**
         * @return True when a consuming subscriber took the event.
         */
        bool emit(const std::string& event, const std::shared_ptr<void>& obj, const char* cppClass);

    private:
        std::shared_ptr<Context> _context;
        Handle _target;
        long long _payloadCounter;
    };

    /**
     * Turns the map's listener callbacks into facade events. `BaseMapView` has a single listener
     * slot, so this chains: the app's existing listener is still called, before the emit.
     */
    class MapEventBridge : public MapEventListener {
    public:
        /**
         * @param context The context the events are emitted on.
         * @param target The handle of the map these events belong to.
         * @param chained The listener that was already installed, or null.
         */
        MapEventBridge(const std::shared_ptr<Context>& context, Handle target,
                       const std::shared_ptr<MapEventListener>& chained);
        virtual ~MapEventBridge();

        virtual void onMapIdle();
        virtual void onMapMoved(MapMoveReason::MapMoveReason reason);
        virtual void onMapStable(MapMoveReason::MapMoveReason reason);
        virtual void onMapInteraction(const std::shared_ptr<MapInteractionInfo>& mapInteractionInfo);
        virtual void onMapClicked(const std::shared_ptr<MapClickInfo>& mapClickInfo);

    private:
        PayloadEmitter _emitter;
        // DirectorPtr: installing a bridge takes the app's own listener out of the SDK's
        // ThreadSafeDirectorPtr slot, so this becomes the only thing pinning its binding peer.
        DirectorPtr<MapEventListener> _chained;
    };

    /**
     * The same for a vector tile layer's clicks; a consuming subscription decides whether
     * onVectorTileClicked reports the click handled.
     */
    class VectorTileEventBridge : public VectorTileEventListener {
    public:
        VectorTileEventBridge(const std::shared_ptr<Context>& context, Handle target,
                              const std::shared_ptr<VectorTileEventListener>& chained);
        virtual ~VectorTileEventBridge();

        virtual bool onVectorTileClicked(const std::shared_ptr<VectorTileClickInfo>& clickInfo);

    private:
        PayloadEmitter _emitter;
        DirectorPtr<VectorTileEventListener> _chained;
    };

    /**
     * The same for a vector layer's element clicks.
     */
    class VectorElementEventBridge : public VectorElementEventListener {
    public:
        VectorElementEventBridge(const std::shared_ptr<Context>& context, Handle target,
                                 const std::shared_ptr<VectorElementEventListener>& chained);
        virtual ~VectorElementEventBridge();

        virtual bool onVectorElementClicked(const std::shared_ptr<VectorElementClickInfo>& clickInfo);

    private:
        PayloadEmitter _emitter;
        DirectorPtr<VectorElementEventListener> _chained;
    };

} }

#endif
