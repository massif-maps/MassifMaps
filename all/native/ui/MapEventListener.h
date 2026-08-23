/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPEVENTLISTENER_H_
#define _MASSIF_MAPEVENTLISTENER_H_

#include "ui/MapMoveReason.h"

#include <memory>

namespace massif {
    class MapClickInfo;
    class MapInteractionInfo;

    /**
     * Listener for events like map clicks etc.
     */
    class MapEventListener {
    public:
        virtual ~MapEventListener() { }
    
        /**
         * Listener method that gets called at the end of the rendering process when the
         * map view needs no further refreshing.
         * Note that there can still be background processes (tile loading) that may change
         * the map view but these may take long time.
         * This method is called from GL renderer thread, not from main thread.
         */
        virtual void onMapIdle() { }

        /**
         * Listener method that gets called when the map is panned, rotated, tilted or zoomed.
         * The callback is used for both UI events and map changes resulting from API calls;
         * the reason says which.
         * Doing any calls to update MapView state from this method is potentially dangerous and may
         * result in deadlocks or crashes.
         * The thread this method is called from may vary.
         * @param reason What caused the camera change.
         */
        virtual void onMapMoved(MapMoveReason::MapMoveReason reason) { }

        /**
         * Listener method that gets called when the map comes to rest - animations have finished,
         * the user has lifted their fingers from the screen and any inertia has died out.
         *
         * This is the end of a movement, not a poll: it is called once per movement, and a touch
         * that did not move the camera does not call it at all. It concerns the CAMERA only -
         * tiles may still be loading, so it is not "everything is drawn"; that is onMapIdle.
         *
         * The thread this method is called from may vary.
         * @param reason What caused the movement that just ended.
         */
        virtual void onMapStable(MapMoveReason::MapMoveReason reason) { }
    
        /**
         * Listener method that gets called when user has interacted with the map. The callback
         * includes info about interaction type (panning, zooming, etc).
         * @param mapInteractionInfo A container that provides information about the interaction.
         */
        virtual void onMapInteraction(const std::shared_ptr<MapInteractionInfo>& mapInteractionInfo) { }
        
        /**
         * Listener method that gets called when a click is performed on an empty area of the map.
         * This method will NOT be called from the main thread.
         * @param mapClickInfo A container that provides information about the click.
         */
        virtual void onMapClicked(const std::shared_ptr<MapClickInfo>& mapClickInfo) { }
    };
    
}

#endif
