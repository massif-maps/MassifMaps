/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_EVENTLISTENER_H_
#define _MASSIF_API_EVENTLISTENER_H_

#include <string>

namespace massif { namespace api {

    /**
     * What an app implements to receive facade events.
     *
     * A director, so a Java or Objective-C subclass works. The C ABI uses a function pointer for
     * the same job.
     */
    class EventListener {
    public:
        virtual ~EventListener() { }

        /**
         * Called when a subscribed event fires.
         * @param target The object the event belongs to.
         * @param event The event name, e.g. "map.clicked".
         * @param payload The event's data, or 0 when it carries none. Read it with the property
         *                verbs; it is valid until this call returns.
         * @return True to consume the event, when the subscription asked to consume.
         */
        virtual bool onEvent(int target, const std::string& event, int payload) { return false; }
    };

} }

#endif
