/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_LIGHTOPTIONS_H_
#define _MASSIF_LIGHTOPTIONS_H_

#include "graphics/Color.h"
#include "components/LightStop.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cglib/vec.h>

namespace massif {

    /**
     * Directional light (sun) configuration, attached to the map via Options::setLightOptions.
     * The sun direction drives the sky shader, terrain surface lighting and shadows.
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class LightOptions {
    public:
        /**
         * Interface for monitoring light option change events. Internal.
         */
        struct OnChangeListener {
            virtual ~OnChangeListener() { }

            /**
             * Listener method that gets called when a light option has changed.
             * @param optionName The name of the option that has changed.
             */
            virtual void onLightOptionChanged(const std::string& optionName) = 0;
        };

        /**
         * Constructs a LightOptions object with default values.
         */
        LightOptions();
        virtual ~LightOptions();

        /**
         * Returns the sun azimuth in degrees.
         * @return The sun azimuth in degrees, clockwise from north. The default is 315 (north-west).
         */
        float getSunAzimuth() const;
        /**
         * Sets the sun azimuth in degrees, measured clockwise from north (0 = north, 90 = east).
         * The classic cartographic hillshade light comes from the north-west, which is the default.
         * @param azimuth The new sun azimuth in degrees.
         */
        void setSunAzimuth(float azimuth);

        /**
         * Returns the sun altitude in degrees above the horizon.
         * @return The sun altitude in degrees. The default is 45.
         */
        float getSunAltitude() const;
        /**
         * Sets the sun altitude in degrees above the horizon (0 = at the horizon, 90 = zenith).
         * Negative values put the sun below the horizon (night).
         * @param altitude The new sun altitude in degrees (clamped to -90..90).
         */
        void setSunAltitude(float altitude);

        /**
         * Sets the sun position from a date, a time and a location, using the standard
         * solar position algorithm. This is a convenience wrapper that computes and stores
         * the azimuth and the altitude; reading them back returns the computed values.
         * @param year The year (for example 2026).
         * @param month The month, 1..12.
         * @param day The day of the month, 1..31.
         * @param hour The hour in UTC, 0..23.
         * @param minute The minute, 0..59.
         * @param latitude The observer latitude in degrees.
         * @param longitude The observer longitude in degrees.
         */
        void setSunPositionFromTime(int year, int month, int day, int hour, int minute, double latitude, double longitude);

        /**
         * Returns the sun (directional light) color.
         * @return The sun color. The default is white.
         */
        Color getSunColor() const;
        /**
         * Sets the sun (directional light) color.
         * @param color The new sun color.
         */
        void setSunColor(const Color& color);

        /**
         * Returns the sun light intensity.
         * @return The sun intensity. The default is 1.
         */
        float getSunIntensity() const;
        /**
         * Sets the sun light intensity, a multiplier on the directional contribution.
         * @param intensity The new sun intensity (clamped to 0..8).
         */
        void setSunIntensity(float intensity);

        /**
         * Returns the ambient light intensity.
         * @return The ambient intensity. The default is 0.35.
         */
        float getAmbientIntensity() const;
        /**
         * Sets the ambient light intensity, the amount of light reaching surfaces that face
         * away from the sun. This is also the brightness floor inside shadows.
         * @param intensity The new ambient intensity (clamped to 0..1).
         */
        void setAmbientIntensity(float intensity);

        /**
         * Returns the ambient light color.
         * @return The ambient color. The default is white.
         */
        Color getAmbientColor() const;
        /**
         * Sets the ambient light color - the tint of the light reaching surfaces that face away
         * from the sun, i.e. the colour of everything in shadow. White keeps the neutral grey
         * shading; a cool blue is what makes a dusk or night scene read as lit by the sky rather
         * than simply darker. Applies to the terrain surface and to 3D buildings alike.
         * @param color The new ambient color.
         */
        void setAmbientColor(const Color& color);

        /**
         * Returns whether this sun overrides the one a style states.
         * @return True if the application's sun wins over the style's. The default is false.
         */
        bool isSunOverridingStyle() const;
        /**
         * Sets whether this sun overrides the one a style states.
         *
         * A style may state its own sun - a converted MapBox style does, one direction per light
         * preset - and by default that is what lights the map, so it looks as its source does with
         * no application code at all. An application that moves the sun itself, a day/night cycle
         * being the usual reason, sets this and its own azimuth and altitude win instead.
         *
         * Only the DIRECTION is affected. Intensities and colours merge as before.
         * @param overriding True to let this object's sun win over the style's.
         */
        void setSunOverridingStyle(bool overriding);

        /**
         * Returns whether the sun's COLOURS follow its position.
         * @return True if the light colours are derived from the sun's height. The default is false.
         */
        bool isDayCycleLightsEnabled() const;
        /**
         * Sets whether the light COLOURS follow the sun's position instead of being stated.
         *
         * A map that moves its sun with the clock wants the light to change with it: warm and low
         * at dawn, white overhead, orange against a blue sky at dusk, and a dim blue at night. With
         * this on, the ambient and sun colours and their intensities are derived from the sun's own
         * height, interpolated between the four light setups MapBox Standard ships - so an hour of
         * 12 renders as its `day` preset and 19 as its `dusk`, with everything in between.
         *
         * It replaces what the style and this object state for those four values; the DIRECTION is
         * still whatever the sun position says. Off, nothing is derived and the values are taken as
         * before.
         * @param enabled True to derive the light colours from the sun's height.
         */
        void setDayCycleLightsEnabled(bool enabled);

        /**
         * Returns the day-cycle light curve - the "formula" an hour is turned into a look by.
         * @return The stops, sorted by sun height. Empty means the built-in MapBox Standard curve.
         */
        std::vector<LightStop> getDayCycleLightStops() const;
        /**
         * Sets the day-cycle light curve, replacing the built-in one.
         *
         * The list IS the formula: every colour on the map is derived from the light it returns -
         * the grade a 2D surface takes, the sun and ambient a building and the terrain are lit
         * with, and the brightness a style ramps its labels over - so one list changes the whole
         * palette at every hour, in 2D and in 3D, with no second theme and no re-decode.
         *
         * Stops are read in the order given and should be sorted by sun height; below the first and
         * above the last the curve holds, and between two it interpolates in linear colour space.
         * Pass an empty list to go back to the built-in curve, which is MapBox Standard's own.
         *
         * Only used while DayCycleLightsEnabled is on.
         * @param stops The stops, sorted by sun height.
         */
        void setDayCycleLightStops(const std::vector<LightStop>& stops);

        /**
         * Returns the curve used while the sun is RISING, if the app set one.
         * @return The rising stops. Empty means the setting curve is used for both.
         */
        std::vector<LightStop> getDayCycleRisingLightStops() const;
        /**
         * Sets a separate curve for a RISING sun, so dawn need not look like dusk.
         *
         * Nothing but the direction of travel distinguishes the two at the same sun height, and
         * MapBox states them as different lights - dawn warm and bright, dusk cold. Left empty, the
         * one curve is used all day.
         * @param stops The stops, sorted by sun height.
         */
        void setDayCycleRisingLightStops(const std::vector<LightStop>& stops);

        /**
         * Returns whether the sun lights the 3D terrain surface.
         * @return True if terrain surface lighting is enabled. The default is false.
         */
        bool isTerrainLightingEnabled() const;
        /**
         * Sets whether the sun lights the 3D terrain surface. When enabled, the terrain
         * surface shader computes the slope from the elevation data and shades the map with
         * the current sun position - a live hillshade that follows the time of day, replacing
         * the pre-baked hillshade raster layer for the common case. Requires 3D terrain with
         * draping enabled (TerrainOptions.setDrapeFillsEnabled).
         * @param enabled True to light the terrain surface with the sun.
         */
        void setTerrainLightingEnabled(bool enabled);

        /**
         * Returns the shadow strength.
         * @return The shadow strength. The default is 1 (MapBox's own shadow-intensity default).
         */
        float getShadowStrength() const;
        /**
         * Sets how strongly the sun's shadows darken the terrain. Shadows are cast by the terrain
         * itself onto the terrain, so ridges shade valleys at low sun. Requires terrain lighting.
         *
         * NOT the depth drawn: a shadow only hides the direct light, so this is multiplied by the
         * sun's share of the scene light, which is 0 once the sun is under the horizon. 1 is
         * therefore the physically correct shadow - MapBox's - and not a maximum: values above it
         * exaggerate, and are clamped where the two are resolved together.
         * @param strength The new shadow strength (0 = off, 1 = physical; negatives clamped away).
         */
        void setShadowStrength(float strength);

        /**
         * Returns the shadow map resolution.
         * @return The shadow map size in pixels, per cascade. The default is 2048.
         */
        int getShadowMapSize() const;
        /**
         * Sets the shadow map resolution in pixels, per cascade. Higher is sharper and costs
         * more memory (size * size * 4 bytes per cascade) and fill rate. The cascades share one
         * texture, so the size is clamped to what fits: 4096 / cascades.
         * @param size The new shadow map size (clamped to 256..4096 / cascades).
         */
        void setShadowMapSize(int size);

        /**
         * Returns the number of shadow cascades.
         * @return The cascade count. The default is 3.
         */
        int getShadowCascades() const;
        /**
         * Sets how many shadow map cascades are rendered (1 to 4). One map has to cover
         * everything visible, so at a tilt its texels are metres of ground and shadow edges
         * become staircases. Cascades split the view distance: the near one covers a small
         * region with the same number of texels, the far one - where a screen pixel is tens of
         * metres of ground anyway - keeps the coarse cover. Each cascade costs one more caster
         * pass and one more page of shadow texture.
         * @param cascades The new cascade count (clamped to 1..4).
         */
        void setShadowCascades(int cascades);

        /**
         * Returns the shadow distance.
         * @return The shadow distance, in multiples of the camera-to-focus distance. The default
         *         is 0 (use the built-in 4.5).
         */
        float getShadowDistance() const;
        /**
         * Sets how far shadows reach from the camera, in multiples of the camera-to-focus
         * distance - the same unit FogOptions uses for its range, and mapbox's shadow model. The
         * shadow map has a fixed resolution, so the further shadows reach the coarser its texels;
         * ground beyond the distance simply has no shadows, faded out over the last stretch. The
         * unit is relative on purpose: the camera-to-focus distance follows the zoom, so one value
         * holds from a city to a massif where a metric radius cannot. 0 uses the built-in 4.5.
         * @param distance The new shadow distance, in multiples of the camera-to-focus distance.
         */
        void setShadowDistance(float distance);

        /**
         * Returns the shadow caster margin in tiles.
         * @return The caster margin. The default is 3.
         */
        int getShadowCasterMargin() const;
        /**
         * Sets how many tiles wide the ring of extra shadow casters around the visible ones is.
         * A mountain off screen still casts its shadow into the view, and without the ring that
         * shadow disappears as you zoom in and the mountain leaves the visible set.
         *
         * The ring's REACH is not this value: it is the distance a shadow can be thrown, the
         * relief over the tangent of the sun altitude. This value sets the ring's RESOLUTION - the
         * ring is generated at the coarsest tile zoom that still spans that throw in this many
         * tiles, so the reach holds at every zoom while the count stays bounded. Raising it makes
         * the distant casters finer and costs one caster draw per extra tile; 0 removes the ring.
         * @param margin The new caster margin in tiles (clamped to 0..8).
         */
        void setShadowCasterMargin(int margin);

        /**
         * Returns the shadow softness.
         * @return The PCF radius in shadow-map texels. The default is 1.
         */
        float getShadowSoftness() const;
        /**
         * Sets the shadow edge softness, as a radius in shadow-map texels. Larger values blur the
         * shadow edges, which also hides the stair-stepping of a low-resolution shadow map.
         * @param softness The new softness (clamped to 0..8).
         */
        void setShadowSoftness(float softness);

        /**
         * Returns the shadow depth bias.
         * @return The shadow depth bias in meters. The default is 0.5.
         */
        float getShadowBias() const;
        /**
         * Sets the shadow depth bias in meters: the depth slack that keeps a lit surface from
         * shadowing itself. Too small gives acne (dark speckle on lit slopes), too large detaches
         * shadows from what casts them. It is metric on purpose - expressed as a fraction of the
         * light frustum it would grow with the shadowed area, and the shadow would drift away
         * from its caster as the view zoomed out.
         * @param bias The new shadow bias.
         */
        void setShadowBias(float bias);

        /**
         * Returns the shadow normal offset.
         * @return The normal offset in shadow-map texels. The default is 3.
         */
        float getShadowNormalOffset() const;
        /**
         * Sets how far a receiving surface is pushed along its own normal before it looks itself
         * up in the shadow map, in shadow-map texels. This is what keeps a wall from shadowing
         * itself: the sample moves sideways instead of the depth being lifted, so the shadow stays
         * attached to the foot of the building that casts it, where a depth bias large enough to
         * clear the same acne detaches it. Applies to 3D extrusions; the terrain surface takes its
         * normal per fragment and is unaffected. 0 disables it.
         * @param offset The new normal offset in shadow-map texels (clamped to 0..16).
         */
        void setShadowNormalOffset(float offset);

        /**
         * Returns the sun direction as a unit vector in internal map coordinates.
         * The vector points from the surface *towards* the sun. Internal method.
         * @return The unit sun direction.
         */
        cglib::vec3<float> getSunDirection() const;

        /**
         * Registers listener for light option change events. Internal method.
         * @param listener The listener for change events.
         */
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        /**
         * Unregisters listener from light option change events. Internal method.
         * @param listener The previously added listener.
         */
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);

    private:
        void notifyOptionChanged(const std::string& optionName);

        std::atomic<float> _sunAzimuth;
        std::atomic<float> _sunAltitude;
        std::atomic<int> _sunColorARGB;
        std::atomic<float> _sunIntensity;
        std::atomic<float> _ambientIntensity;
        std::atomic<int> _ambientColorARGB;
        std::atomic<bool> _sunOverridesStyle;
        std::atomic<bool> _dayCycleLights;
        std::vector<LightStop> _dayCycleLightStops;
        std::vector<LightStop> _dayCycleRisingLightStops;
        mutable std::mutex _dayCycleLightStopsMutex;
        std::atomic<bool> _terrainLightingEnabled;
        std::atomic<float> _shadowStrength;
        std::atomic<int> _shadowMapSize;
        std::atomic<int> _shadowCascades;
        std::atomic<float> _shadowBias;
        std::atomic<float> _shadowNormalOffset;
        std::atomic<float> _shadowSoftness;
        std::atomic<float> _shadowDistance;
        std::atomic<int> _shadowCasterMargin;

        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _onChangeListenersMutex;
    };

}

#endif
