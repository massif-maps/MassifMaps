/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALEVENTLISTENER_H_
#define _MASSIF_CELESTIALEVENTLISTENER_H_

#include "ui/ClickInfo.h"

#include <memory>

namespace massif {
    class CelestialObject;

    /**
     * Reports clicks on the objects of a CelestialLayer, and on the empty sky around them.
     */
    class CelestialEventListener {
    public:
        virtual ~CelestialEventListener() { }

        /**
         * Called when an object of the layer is clicked. Objects are tested against the touch ray
         * together with every other layer's content, so an object behind terrain is not reported.
         * @param clickInfo The click that hit the object.
         * @param celestialObject The object that was clicked.
         * @return True if the click was handled and must not be passed on, false otherwise.
         */
        virtual bool onCelestialObjectClicked(const ClickInfo& clickInfo, const std::shared_ptr<CelestialObject>& celestialObject) {
            return false;
        }

        /**
         * Called when a click aims at the sky and hits nothing, on no layer: the map listener is not
         * called then, as there is no ground position to report. E.g. to clear a selection.
         * @param clickInfo The click.
         * @param azimuth The azimuth the click aimed at, in degrees clockwise from north.
         * @param altitude The altitude the click aimed at, in degrees above the horizon.
         * @return True if the click was handled and must not be passed on to the other celestial layers.
         */
        virtual bool onSkyClicked(const ClickInfo& clickInfo, float azimuth, float altitude) {
            return false;
        }
    };

}

#endif
