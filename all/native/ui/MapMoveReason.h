/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPMOVEREASON_H_
#define _MASSIF_MAPMOVEREASON_H_

namespace massif {

    namespace MapMoveReason {
        /**
         * What caused a camera change. Carried by every camera event, so an app can tell its own
         * moves from the user's without watching touches itself.
         */
        enum MapMoveReason {
            /**
             * The user, directly: a gesture, a mouse wheel, or the inertia that follows one.
             */
            MAP_MOVE_REASON_GESTURE,
            /**
             * An animation the SDK is stepping - a flight, or a move given a duration. The call
             * that started it reported the reason it was made with; every frame after that is
             * this one.
             */
            MAP_MOVE_REASON_ANIMATION,
            /**
             * The app, through a call that took effect immediately: setFocusPos, setZoom,
             * an option change that moved the camera.
             */
            MAP_MOVE_REASON_API
        };
    }

}

#endif
