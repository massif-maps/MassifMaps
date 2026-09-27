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
         * Listener method that gets called at the end of the rendering process when the map view needs no
         * further refreshing. Background tile loading may still change the view later.
         * Called from the GL renderer thread, not from the main thread.
         */
        virtual void onMapIdle() { }

        /**
         * Listener method that gets called when the map is panned, rotated, tilted or zoomed, by UI events or API calls.
         * Updating MapView state from this method may result in deadlocks or crashes.
         * The thread this method is called from may vary.
         * @param reason What caused the camera change.
         */
        virtual void onMapMoved(MapMoveReason::MapMoveReason reason) { }

        /**
         * Listener method that gets called once per movement when the camera comes to rest (animations finished,
         * fingers lifted, inertia gone); a touch that did not move the camera does not call it.
         * Tiles may still be loading - see onMapIdle for that. The thread this method is called from may vary.
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
         * This method is not called from the main thread.
         * @param mapClickInfo A container that provides information about the click.
         */
        virtual void onMapClicked(const std::shared_ptr<MapClickInfo>& mapClickInfo) { }
    };
    
}

#endif
