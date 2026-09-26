/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VECTORELEMENTEVENTLISTENER_H_
#define _MASSIF_VECTORELEMENTEVENTLISTENER_H_

#include <memory>

namespace massif {
    class VectorElementClickInfo;
    
    /**
     * Listener for vector element events like clicks etc.
     */
    class VectorElementEventListener {
    public:
        virtual ~VectorElementEventListener() { }
    
        /**
         * Listener method that gets called when a click is performed on a vector element. Elements at the click position
         * are called closest to the camera first; returning true stops there, false passes the click to the next one.
         * This method will not be called from the main thread.
         * @param clickInfo A container that provides information about the click.
         * @return True if the click is handled and subsequent elements should not be handled. False if the next element should be called.
         */
        virtual bool onVectorElementClicked(const std::shared_ptr<VectorElementClickInfo>& clickInfo) { return true; }
    };
    
}

#endif
