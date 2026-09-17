/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FLIGHTEASING_H_
#define _MASSIF_FLIGHTEASING_H_

namespace massif {

    namespace FlightEasing {
        /**
         * The timing curve a flyTo runs its clock through. The CSS curves, by their CSS names.
         */
        enum FlightEasing {
            /**
             * cubic-bezier(0.25, 0.1, 0.25, 1), the CSS "ease". The default, and mapbox-gl's.
             */
            FLIGHT_EASING_EASE,
            /**
             * No easing: the constant speed Van Wijk prescribes. Starts and stops abruptly.
             */
            FLIGHT_EASING_LINEAR,
            /**
             * cubic-bezier(0.42, 0, 1, 1). Gentle start, arrives at full speed.
             */
            FLIGHT_EASING_EASE_IN,
            /**
             * cubic-bezier(0, 0, 0.58, 1). Starts at full speed, comes to a stop.
             */
            FLIGHT_EASING_EASE_OUT,
            /**
             * cubic-bezier(0.42, 0, 0.58, 1). Symmetric, stronger than FLIGHT_EASING_EASE.
             */
            FLIGHT_EASING_EASE_IN_OUT
        };
    }

}

#endif
