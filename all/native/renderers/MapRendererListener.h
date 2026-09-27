/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPRENDERERLISTENER_H_
#define _MASSIF_MAPRENDERERLISTENER_H_

namespace massif {

    /**
     * Listener for specific map renderer events.
     */
    class MapRendererListener {
    public:
        virtual ~MapRendererListener() { }
        
        /**
         * Called when the rendering surface is initialized or resized; from then on view-size dependent
         * methods (moveToFitBounds, screenToMap, mapToScreen) are safe to call.
         * Called from the GL renderer thread, not the main thread.
         */
        virtual void onSurfaceChanged(int width, int height) { }
        
        /**
         * Called at the start of the rendering frame, e.g. to keep a marker at the focus point.
         * Called from the GL renderer thread, not the main thread.
         */
        virtual void onBeforeDrawFrame() { }
        
        /**
         * Listener method that gets called at the end of the rendering frame.
         * This method is called from GL renderer thread, not from main thread.
         */
        virtual void onAfterDrawFrame() { }
    };
    
}

#endif
