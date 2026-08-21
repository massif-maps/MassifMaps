/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_MAPEVENTBRIDGE_H_
#define _MASSIF_API_MAPEVENTBRIDGE_H_

#include "api/Context.h"
#include "ui/MapEventListener.h"

#include <memory>
#include <string>

namespace massif { namespace api {

    /**
     * Turns the map's listener callbacks into facade events.
     *
     * `BaseMapView` has a single listener slot, so this **chains**: whatever the app already
     * installed keeps being called, before the event is emitted. Otherwise adopting the facade
     * would silently disconnect an app's existing handlers.
     *
     * The payload is a real registry object for the duration of the emit - registered, emitted,
     * then dropped. A queued handler holds it alive through the normal retain, so the id going
     * away does not free it early.
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
        virtual void onMapMoved();
        virtual void onMapStable();
        virtual void onMapInteraction(const std::shared_ptr<MapInteractionInfo>& mapInteractionInfo);
        virtual void onMapClicked(const std::shared_ptr<MapClickInfo>& mapClickInfo);

    private:
        /** Registers obj under a throwaway id, emits, and drops the id again. */
        void emitWith(const std::string& event, const std::shared_ptr<void>& obj,
                      const char* cppClass);

        std::shared_ptr<Context> _context;
        Handle _target;
        std::shared_ptr<MapEventListener> _chained;
        long long _payloadCounter;
    };

} }

#endif
