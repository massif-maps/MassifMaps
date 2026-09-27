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
         * Sets the sun position from a date, a time and a location, using the standard solar position
         * algorithm; stores the computed azimuth and altitude.
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
         * @return The ambient intensity. The default is 1.
         */
        float getAmbientIntensity() const;
        /**
         * Sets the ambient light intensity: light reaching surfaces facing away from the sun, and the
         * brightness floor inside shadows.
         * @param intensity The new ambient intensity (clamped to 0..1).
         */
        void setAmbientIntensity(float intensity);

        /**
         * Returns the ambient light color.
         * @return The ambient color. The default is white.
         */
        Color getAmbientColor() const;
        /**
         * Sets the ambient light color: the tint of everything in shadow, on terrain and 3D buildings.
         * White keeps neutral grey shading; a cool blue reads as sky-lit dusk or night.
         * @param color The new ambient color.
         */
        void setAmbientColor(const Color& color);

        /**
         * Returns whether the sun intensity was set; 3D extrusions follow only a stated sun, so an unlit
         * style's roofs do not sum past full light. Not bound - for StyleEnvironment::resolveLighting.
         * @return True if setSunIntensity has been called.
         */
        bool isSunIntensityStated() const;

        /**
         * Returns whether this sun overrides the one a style states.
         * @return True if the application's sun wins over the style's. The default is false.
         */
        bool isSunOverridingStyle() const;
        /**
         * Sets whether this sun's direction overrides the one a style states (e.g. for an app-driven
         * day/night cycle). Intensities and colours merge as before.
         * @param overriding True to let this object's sun win over the style's.
         */
        void setSunOverridingStyle(bool overriding);

        /**
         * Returns whether the sun's COLOURS follow its position.
         * @return True if the light colours are derived from the sun's height. The default is false.
         */
        bool isDayCycleLightsEnabled() const;
        /**
         * Sets whether the ambient and sun colours and intensities are derived from the sun's height
         * (the day-cycle curve), replacing what the style and this object state. The direction is unaffected.
         * @param enabled True to derive the light colours from the sun's height.
         */
        void setDayCycleLightsEnabled(bool enabled);

        /**
         * Returns the day-cycle light curve.
         * @return The stops, sorted by sun height. Empty means the built-in MapBox Standard curve.
         */
        std::vector<LightStop> getDayCycleLightStops() const;
        /**
         * Sets the day-cycle light curve, from which every map colour at every hour derives. Clamped at
         * the ends, interpolated in linear colour space between; empty restores the built-in (MapBox
         * Standard) curve. Only used while DayCycleLightsEnabled is on.
         * @param stops The stops, sorted by sun height.
         */
        void setDayCycleLightStops(const std::vector<LightStop>& stops);

        /**
         * Returns the curve used while the sun is RISING, if the app set one.
         * @return The rising stops. Empty means the setting curve is used for both.
         */
        std::vector<LightStop> getDayCycleRisingLightStops() const;
        /**
         * Sets a separate curve for a rising sun, so dawn need not look like dusk. Empty uses the one
         * curve all day.
         * @param stops The stops, sorted by sun height.
         */
        void setDayCycleRisingLightStops(const std::vector<LightStop>& stops);

        /**
         * Returns whether the sun lights the 3D terrain surface.
         * @return True if terrain surface lighting is enabled. The default is false.
         */
        bool isTerrainLightingEnabled() const;
        /**
         * Sets whether the sun lights the 3D terrain surface: a live hillshade following the sun.
         * Requires 3D terrain with draping enabled (TerrainOptions.setDrapeFillsEnabled).
         * @param enabled True to light the terrain surface with the sun.
         */
        void setTerrainLightingEnabled(bool enabled);

        /**
         * Returns the shadow strength.
         * @return The shadow strength. The default is 1 (MapBox's own shadow-intensity default).
         */
        float getShadowStrength() const;
        /**
         * Sets how strongly the sun's shadows darken the terrain; requires terrain lighting. Scaled by the
         * sun's share of the light (0 below the horizon), so 1 is the physical (MapBox) shadow, not a maximum.
         * @param strength The new shadow strength (0 = off, 1 = physical; negatives clamped away).
         */
        void setShadowStrength(float strength);

        /**
         * Returns the shadow map resolution.
         * @return The shadow map size in pixels, per cascade. The default is 2048.
         */
        int getShadowMapSize() const;
        /**
         * Sets the shadow map resolution in pixels, per cascade; costs size * size * 4 bytes per cascade.
         * The cascades share one texture, so the renderer also caps it at the max texture size / cascades.
         * @param size The new shadow map size (clamped to 256..4096).
         */
        void setShadowMapSize(int size);

        /**
         * Returns the number of shadow cascades.
         * @return The cascade count. The default is 2, as mapbox uses.
         */
        int getShadowCascades() const;
        /**
         * Sets how many shadow map cascades split the view distance, sharpening near shadows at a tilt.
         * Each costs one more caster pass and one more shadow texture page.
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
         * Sets how far shadows reach, in multiples of the camera-to-focus distance (as FogOptions ranges),
         * so one value holds at every zoom. Further is coarser; beyond it shadows fade out. 0 uses 4.5.
         * @param distance The new shadow distance, in multiples of the camera-to-focus distance.
         */
        void setShadowDistance(float distance);

        /**
         * Returns the shadow caster margin in tiles.
         * @return The caster margin. The default is 3.
         */
        int getShadowCasterMargin() const;
        /**
         * Sets the resolution of the ring of off-screen shadow casters: its reach is the shadow throw,
         * spanned in this many tiles. Higher is finer and costs one caster draw per tile; 0 removes the ring.
         * @param margin The new caster margin in tiles (clamped to 0..8).
         */
        void setShadowCasterMargin(int margin);

        /**
         * Returns the shadow softness.
         * @return The PCF radius in shadow-map texels. The default is 1.
         */
        float getShadowSoftness() const;
        /**
         * Sets the shadow edge softness, as a PCF radius in shadow-map texels.
         * @param softness The new softness (clamped to 0..8).
         */
        void setShadowSoftness(float softness);

        /**
         * Returns the shadow depth bias scale.
         * @return The scale on MapBox's shadow bias. The default is 1 (theirs unchanged).
         */
        float getShadowBias() const;
        /**
         * Scales MapBox's shadow depth bias (unitless; 1 = theirs). Too small gives acne, too large
         * detaches shadows from their casters.
         * @param bias The new shadow bias scale (clamped to 0..50).
         */
        void setShadowBias(float bias);

        /**
         * Returns the shadow normal offset.
         * @return The normal offset in shadow-map texels. The default is 3.
         */
        float getShadowNormalOffset() const;
        /**
         * Sets how far a receiver is pushed along its normal before the shadow lookup, which clears wall
         * acne without detaching the shadow. 3D extrusions only; 0 disables it.
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
        std::atomic<bool> _sunIntensityStated;
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
