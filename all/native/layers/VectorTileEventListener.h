/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VECTORTILEEVENTLISTENER_H_
#define _MASSIF_VECTORTILEEVENTLISTENER_H_

#include <memory>

namespace massif {
    class VectorTileClickInfo;
    
    /**
     * Listener for vector tile element events like clicks etc.
     */
    class VectorTileEventListener {
    public:
        virtual ~VectorTileEventListener() { }
    
        /**
         * Listener method that gets called when a click is performed on a vector tile feature. Elements at the click position
         * are called closest to the camera first; returning true stops there, false passes the click to the next one.
         * This method will not be called from the main thread.
         * @param clickInfo A container that provides information about the click.
         * @return True if the click is handled and subsequent elements should not be handled. False if the next element should be called.
         */
        virtual bool onVectorTileClicked(const std::shared_ptr<VectorTileClickInfo>& clickInfo) { return true; }
    };
    
}

#endif
