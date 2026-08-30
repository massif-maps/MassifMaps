/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_LIGHTSTOP_H_
#define _MASSIF_LIGHTSTOP_H_

#include "graphics/Color.h"

#include <string>

namespace massif {

    /**
     * One point on a day-cycle light curve: the scene light at a given sun height.
     *
     * A list of these IS the "formula" that turns an hour into a look. Everything downstream is
     * derived from the two lights it names - the colour every 2D surface is graded by, the sun and
     * ambient the 3D buildings and the terrain are lit with, and the brightness a style reads as
     * `view::brightness` - so replacing the list replaces the whole map's palette at every hour,
     * in 2D and in 3D, without a second theme and without a re-decode.
     *
     * The default list is MapBox Standard's own four light setups, which is why a converted
     * Standard renders as its `day` preset at noon and its `dusk` preset at 19h.
     */
    class LightStop {
    public:
        /**
         * Constructs an empty LightStop, a white light at the horizon.
         */
        LightStop();
        /**
         * Constructs a LightStop.
         * @param sunAltitude The sun height this light belongs to, in degrees above the horizon.
         * @param ambientColor The ambient (sky) colour.
         * @param ambientIntensity The ambient intensity, 0-1.
         * @param sunColor The directional (sun) colour.
         * @param sunIntensity The directional intensity, 0-1.
         */
        LightStop(float sunAltitude, const Color& ambientColor, float ambientIntensity, const Color& sunColor, float sunIntensity);

        /**
         * Returns the sun height this light belongs to.
         * @return The sun height, in degrees above the horizon.
         */
        float getSunAltitude() const;
        /**
         * Returns the ambient colour.
         * @return The ambient colour.
         */
        const Color& getAmbientColor() const;
        /**
         * Returns the ambient intensity.
         * @return The ambient intensity, 0-1.
         */
        float getAmbientIntensity() const;
        /**
         * Returns the directional colour.
         * @return The directional colour.
         */
        const Color& getSunColor() const;
        /**
         * Returns the directional intensity.
         * @return The directional intensity, 0-1.
         */
        float getSunIntensity() const;

        bool operator ==(const LightStop& other) const;
        bool operator !=(const LightStop& other) const;

        /**
         * Returns the hash value of this object.
         * @return The hash value of this object.
         */
        int hash() const;

        /**
         * Creates a string representation of this light stop, useful for logging.
         * @return The string representation of this light stop.
         */
        std::string toString() const;

    private:
        float _sunAltitude;
        Color _ambientColor;
        float _ambientIntensity;
        Color _sunColor;
        float _sunIntensity;
    };

}

#endif
