/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FOGOPTIONS_H_
#define _MASSIF_FOGOPTIONS_H_

#include "graphics/Color.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace massif {

    /**
     * The atmosphere (haze, upper-sky colours, stars), modelled on the Mapbox "fog" property. Ranges are
     * in multiples of the camera-to-focus distance; the default colour is transparent (no fog).
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class FogOptions {
    public:
        /**
         * Interface for monitoring fog option change events. Internal.
         */
        struct OnChangeListener {
            virtual ~OnChangeListener() { }

            /**
             * Listener method that gets called when a fog option has changed.
             * @param optionName The name of the option that has changed.
             */
            virtual void onFogOptionChanged(const std::string& optionName) = 0;
        };

        /**
         * Constructs a FogOptions object with default values.
         */
        FogOptions();
        virtual ~FogOptions();

        /**
         * Returns whether the fog is drawn at all.
         * @return True if the fog is drawn. The default is true.
         */
        bool isEnabled() const;
        /**
         * Enables or disables the haze everywhere without touching any value; HighColor, SpaceColor and
         * the stars are unaffected. ANDed with the style, so a style cannot re-enable it.
         * Style property: "fog-enabled" (0 or 1).
         * @param enabled True to draw the fog.
         */
        void setEnabled(bool enabled);

        /**
         * Returns the fog color.
         * @return The fog color. The default is transparent (no fog).
         */
        Color getColor() const;
        /**
         * Sets the color distant content fades towards; alpha is the opacity at full distance, so
         * transparent (the default) means no fog. With terrain lighting on it is lit by the sun first.
         * Style property: "fog-color".
         * @param color The new fog color.
         */
        void setColor(const Color& color);

        /**
         * Returns where the fog starts.
         * @return The start of the range, in multiples of the camera-to-focus distance. The default is 0.8.
         */
        float getRangeStart() const;
        /**
         * Sets where the fog starts, in multiples of the camera-to-focus distance. Mapbox range[0].
         * Style property: "fog-range-start".
         * @param rangeStart The new start of the range (clamped to 0 and above).
         */
        void setRangeStart(float rangeStart);

        /**
         * Returns where the fog reaches full strength.
         * @return The end of the range, in multiples of the camera-to-focus distance. The default is 8.
         */
        float getRangeEnd() const;
        /**
         * Sets where the fog reaches full strength, in multiples of the camera-to-focus distance.
         * At or below RangeStart turns the fog off. Mapbox range[1]. Style property: "fog-range-end".
         * @param rangeEnd The new end of the range (clamped to 0 and above).
         */
        void setRangeEnd(float rangeEnd);

        /**
         * Returns the color of the upper atmosphere.
         * @return The high color. The default is transparent, which leaves the sky to SkyOptions.
         */
        Color getHighColor() const;
        /**
         * Sets the sky color above the fog band - Mapbox high-color. Transparent (the default) leaves
         * the sky to SkyOptions. Style property: "fog-high-color".
         * @param color The new high color.
         */
        void setHighColor(const Color& color);

        /**
         * Returns the color of the sky at the zenith, beyond the atmosphere.
         * @return The space color. The default is transparent, which leaves the sky to SkyOptions.
         */
        Color getSpaceColor() const;
        /**
         * Sets the sky color at the zenith - Mapbox space-color. Transparent (the default) leaves
         * the sky to SkyOptions. Style property: "fog-space-color".
         * @param color The new space color.
         */
        void setSpaceColor(const Color& color);

        /**
         * Returns how far up the sky the fog is blended in.
         * @return The blend, 0 to 1. The default is 0.133.
         */
        float getHorizonBlend() const;
        /**
         * Sets how far above the horizon the fog fades out (Mapbox horizon-blend): scaled by
         * exp(-3 * (sin(elevation) / blend)^2), 1 below the horizon so ground and sky meet seamlessly.
         * Style property: "fog-horizon-blend".
         * @param horizonBlend The new blend (clamped to 0..1).
         */
        void setHorizonBlend(float horizonBlend);

        /**
         * Returns the altitude the fog starts fading out at.
         * @return The altitude in meters. The default is 0.
         */
        float getVerticalRangeStart() const;
        /**
         * Sets the altitude above sea level where the fog starts to fade out - Mapbox vertical-range[0].
         * Both at 0 (the default) fogs every altitude equally. Style property: "fog-vertical-range-start".
         * @param startMeters The new altitude in meters (clamped to 0 and above).
         */
        void setVerticalRangeStart(float startMeters);

        /**
         * Returns the altitude the fog has fully faded out at.
         * @return The altitude in meters. The default is 0.
         */
        float getVerticalRangeEnd() const;
        /**
         * Sets the altitude above sea level where the fog has fully faded out - Mapbox vertical-range[1].
         * At or below VerticalRangeStart disables the fade. Style property: "fog-vertical-range-end".
         * @param endMeters The new altitude in meters (clamped to 0 and above).
         */
        void setVerticalRangeEnd(float endMeters);

        /**
         * Returns how brightly stars are drawn beyond the atmosphere.
         * @return The star intensity, 0 to 1. The default is 0 (no stars).
         */
        float getStarIntensity() const;
        /**
         * Sets how brightly stars are drawn beyond the atmosphere - Mapbox star-intensity. 0 (the default)
         * draws none. Only the built-in sky shader draws them. Style property: "fog-star-intensity".
         * @param starIntensity The new star intensity (clamped to 0..1).
         */
        void setStarIntensity(float starIntensity);

        /**
         * Returns the custom fog fragment shader source, or an empty string if the built-in
         * blend is used.
         * @return The custom shader source.
         */
        std::string getShaderSource() const;
        /**
         * Replaces the whole fog block (tiles, background, terrain, vector elements, sky). Must define
         * `vec4 applyFog(vec4 color, vec3 dir, float dist, float heightM)`, `vec4 skyFog(vec4 color, vec3 dir)`
         * and `float fogLabelFade()`; uniforms/helpers are predeclared - see docs/features/sky-sun-shadows.md.
         * @param shaderSource The GLSL source, or an empty string for the built-in blend (also used, and the error logged, if it fails to compile).
         */
        void setShaderSource(const std::string& shaderSource);

        /**
         * Registers listener for fog option change events. Internal method.
         * @param listener The listener for change events.
         */
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        /**
         * Unregisters listener from fog option change events. Internal method.
         * @param listener The previously added listener.
         */
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);

    private:
        void notifyOptionChanged(const std::string& optionName);

        std::atomic<bool> _enabled;
        std::atomic<int> _colorARGB;
        std::atomic<float> _rangeStart;
        std::atomic<float> _rangeEnd;
        std::atomic<int> _highColorARGB;
        std::atomic<int> _spaceColorARGB;
        std::atomic<float> _horizonBlend;
        std::atomic<float> _verticalRangeStart;
        std::atomic<float> _verticalRangeEnd;
        std::atomic<float> _starIntensity;

        std::string _shaderSource;
        mutable std::mutex _shaderSourceMutex;

        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _onChangeListenersMutex;
    };

}

#endif
