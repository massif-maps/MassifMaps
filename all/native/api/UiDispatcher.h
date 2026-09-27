/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_UIDISPATCHER_H_
#define _MASSIF_API_UIDISPATCHER_H_

namespace massif { namespace api {

    /**
     * How the facade reaches an app's UI thread: UI-delivery subscriptions are queued and post() wakes
     * the queue. Without one, they run inline on the producing thread (logged once).
     */
    class UiDispatcher {
    public:
        virtual ~UiDispatcher() { }

        /**
         * Called from the producing thread. Get onto the UI thread and call MassifApi::drain.
         * May be called again before a previous drain has run; one drain empties the whole queue.
         */
        virtual void post() { }
    };

} }

#endif
