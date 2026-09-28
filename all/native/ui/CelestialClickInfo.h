/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALCLICKINFO_H_
#define _MASSIF_CELESTIALCLICKINFO_H_

#include "ui/ClickInfo.h"

#include <memory>

namespace massif {
    class CelestialObject;

    /**
     * A container class that provides information about a click performed on
     * an object of a CelestialLayer.
     */
    class CelestialClickInfo {
    public:
        /**
         * Constructs a CelestialClickInfo object from a click info and the clicked object.
         * @param clickInfo The click info.
         * @param celestialObject The object on which the click was performed.
         */
        CelestialClickInfo(const ClickInfo& clickInfo, const std::shared_ptr<CelestialObject>& celestialObject);
        virtual ~CelestialClickInfo();

        /**
         * Returns the click type.
         * @return The type of the click performed.
         */
        ClickType::ClickType getClickType() const;

        /**
         * Returns the click info.
         * @return The attributes of the click.
         */
        const ClickInfo& getClickInfo() const;

        /**
         * Returns the clicked object.
         * @return The object on which the click was performed.
         */
        std::shared_ptr<CelestialObject> getCelestialObject() const;

        /**
         * Returns the azimuth of the clicked object at the time of the click.
         * @return The azimuth in degrees, clockwise from north.
         */
        float getAzimuth() const;

        /**
         * Returns the altitude of the clicked object at the time of the click.
         * @return The altitude in degrees above the horizon.
         */
        float getAltitude() const;

    private:
        ClickInfo _clickInfo;
        std::shared_ptr<CelestialObject> _celestialObject;
        float _azimuth;
        float _altitude;
    };

}

#endif
