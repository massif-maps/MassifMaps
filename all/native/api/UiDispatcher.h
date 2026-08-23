/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_UIDISPATCHER_H_
#define _MASSIF_API_UIDISPATCHER_H_

namespace massif { namespace api {

    /**
     * How the facade reaches an app's UI thread.
     *
     * A subscription that asked for UI delivery is queued rather than run where the event was
     * produced, and this is what wakes the queue: post() is called from the producing thread and
     * must arrange for MassifApi::drain to run on the UI thread.
     *
     * Without one, UI subscriptions run INLINE on whatever thread produced the event and the
     * facade says so once - delivering on the wrong thread beats dropping the event, but it is not
     * what the subscription asked for.
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
