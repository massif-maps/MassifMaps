/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SKYOPTIONS_H_
#define _MASSIF_SKYOPTIONS_H_

#include "graphics/Color.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace massif {

    namespace SkyType {
        /**
         * Possible sky appearances.
         */
        enum SkyType {
            /**
             * A gradient between HorizonColor and SkyColor; pick it for a flat, stylised or brand-coloured sky.
             */
            SKY_TYPE_GRADIENT,
            /**
             * Rayleigh and Mie single scattering, integrated along the view ray, so the sky follows
             * the time of day on its own.
             */
            SKY_TYPE_ATMOSPHERE
        };
    }

    namespace SkyQuality {
        /**
         * How finely the atmosphere is integrated. The cost is per fragment of visible sky.
         */
        enum SkyQuality {
            /**
             * 5 samples along the view ray, 3 towards the sun.
             */
            SKY_QUALITY_LOW,
            /**
             * 8 and 4. The default.
             */
            SKY_QUALITY_MEDIUM,
            /**
             * 12 and 5.
             */
            SKY_QUALITY_HIGH
        };
    }

    /**
     * Shader-based sky configuration, attached to the map via Options::setSkyOptions: one full-screen
     * pass, a physical atmosphere (default) or a two-colour gradient, lit by LightOptions and hazed by FogOptions.
     * Custom shader contract and uniforms: docs/features/sky-sun-shadows.md.
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class SkyOptions {
    public:
        /**
         * Interface for monitoring sky option change events. Internal.
         */
        struct OnChangeListener {
            virtual ~OnChangeListener() { }

            /**
             * Listener method that gets called when a sky option has changed.
             * @param optionName The name of the option that has changed.
             */
            virtual void onSkyOptionChanged(const std::string& optionName) = 0;
        };

        /**
         * Constructs a SkyOptions object with default values.
         */
        SkyOptions();
        virtual ~SkyOptions();

        /**
         * Returns whether the shader sky is enabled.
         * @return True if the shader sky is drawn. The default is true.
         */
        bool isEnabled() const;
        /**
         * Enables or disables the shader sky. When disabled, the legacy sky bitmap band
         * (Options::setSkyColor / the style sky bitmap) is drawn instead.
         * @param enabled True to draw the shader sky.
         */
        void setEnabled(bool enabled);

        /**
         * Returns what the sky pass draws.
         * @return The sky type. The default is SKY_TYPE_ATMOSPHERE.
         */
        SkyType::SkyType getType() const;
        /**
         * Sets what the sky pass draws - a physical atmosphere or the two-colour gradient,
         * which ignores every Atmosphere* property.
         * Style property: "sky-type" (0 gradient, non-zero atmosphere).
         * @param type The new sky type.
         */
        void setType(SkyType::SkyType type);

        /**
         * Returns how finely the atmosphere is integrated.
         * @return The quality. The default is SKY_QUALITY_MEDIUM.
         */
        SkyQuality::SkyQuality getQuality() const;
        /**
         * Sets how finely the atmosphere is integrated; the cost is per fragment of visible sky.
         * Ignored by SKY_TYPE_GRADIENT.
         * @param quality The new quality.
         */
        void setQuality(SkyQuality::SkyQuality quality);

        /**
         * Returns the brightness of the sun driving the atmosphere.
         * @return The sun intensity. The default is 10.
         */
        float getAtmosphereSunIntensity() const;
        /**
         * Sets how bright the scattering model's own sun is (not LightOptions' ground light);
         * raising it brightens the whole sky, not only the disc.
         * Style property: "sky-atmosphere-sun-intensity".
         * @param intensity The new sun intensity (clamped to 0 and above).
         */
        void setAtmosphereSunIntensity(float intensity);

        /**
         * Returns the tint applied to Rayleigh scattering.
         * @return The atmosphere color. The default is opaque white, i.e. no tint.
         */
        Color getAtmosphereColor() const;
        /**
         * Sets the tint of the Rayleigh term - the blue of the sky - as Mapbox sky-atmosphere-color.
         * The alpha channel scales how much of it there is, so a lower alpha thins the atmosphere.
         * Style property: "sky-atmosphere-color".
         * @param color The new atmosphere color.
         */
        void setAtmosphereColor(const Color& color);

        /**
         * Returns the tint applied to Mie scattering.
         * @return The halo color. The default is opaque white, i.e. no tint.
         */
        Color getHaloColor() const;
        /**
         * Sets the tint of the Mie term - the halo around the sun and the whiteness near the
         * horizon - as Mapbox sky-atmosphere-halo-color. The alpha channel scales its strength.
         * Style property: "sky-atmosphere-halo-color".
         * @param color The new halo color.
         */
        void setHaloColor(const Color& color);

        /**
         * Returns the exposure applied to the scattered light.
         * @return The luminance. The default is 1.
         */
        float getAtmosphereLuminance() const;
        /**
         * Sets the exposure the scattered light is tonemapped with. Lower values brighten the sky,
         * which is what a night or a heavily tinted atmosphere needs to stay readable.
         * Style property: "sky-atmosphere-luminance".
         * @param luminance The new luminance (clamped to 0.01 and above).
         */
        void setAtmosphereLuminance(float luminance);

        /**
         * Returns the zenith sky color.
         * @return The sky color. The default is a light blue.
         */
        Color getSkyColor() const;
        /**
         * Sets the zenith sky color, used by the built-in shader at the top of the sky.
         * @param color The new sky color.
         */
        void setSkyColor(const Color& color);

        /**
         * Returns the horizon color.
         * @return The horizon color. The default is a pale blue-white.
         */
        Color getHorizonColor() const;
        /**
         * Sets the horizon color, used by the built-in shader at the horizon line.
         * @param color The new horizon color.
         */
        void setHorizonColor(const Color& color);

        /**
         * Returns the ground color.
         * @return The color drawn below the horizon. The default is the horizon color.
         */
        Color getGroundColor() const;
        /**
         * Sets the color drawn below the horizon; it only shows between the far edge of the drawn map
         * and the mathematical horizon, so keep it close to the horizon color (the default).
         * Setting it transparent leaves the clear color there.
         * @param color The new ground color.
         */
        void setGroundColor(const Color& color);

        /**
         * Returns the angular blend width between the horizon color and the sky color.
         * @return The blend width in degrees. The default is 12.
         */
        float getHorizonBlend() const;
        /**
         * Sets how far above the horizon, in degrees, the horizon color fades into the sky color.
         * @param degrees The new blend width in degrees (clamped to 0..90).
         */
        void setHorizonBlend(float degrees);

        /**
         * Returns whether the built-in shader draws a sun disc.
         * @return True if the sun disc is drawn. The default is true.
         */
        bool isSunDiscEnabled() const;
        /**
         * Enables or disables the sun disc and its glow in the built-in shader.
         * @param enabled True to draw the sun disc.
         */
        void setSunDiscEnabled(bool enabled);

        /**
         * Returns the custom sky fragment shader source, or an empty string if the built-in
         * shader is used.
         * @return The custom shader source.
         */
        std::string getShaderSource() const;
        /**
         * Sets a custom sky fragment shader. The source must define
         * "vec4 skyColor(vec3 rayDir)" and may use the uniforms documented on this class.
         * Pass an empty string to go back to the built-in shader. If the shader fails to
         * compile, the built-in shader is used and the error is logged.
         * @param shaderSource The GLSL source, or an empty string for the built-in shader.
         */
        void setShaderSource(const std::string& shaderSource);

        /**
         * Registers listener for sky option change events. Internal method.
         * @param listener The listener for change events.
         */
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        /**
         * Unregisters listener from sky option change events. Internal method.
         * @param listener The previously added listener.
         */
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);

    private:
        void notifyOptionChanged(const std::string& optionName);

        std::atomic<bool> _enabled;
        std::atomic<int> _type;
        std::atomic<int> _quality;
        std::atomic<float> _atmosphereSunIntensity;
        std::atomic<int> _atmosphereColorARGB;
        std::atomic<int> _haloColorARGB;
        std::atomic<float> _atmosphereLuminance;
        std::atomic<int> _skyColorARGB;
        std::atomic<int> _horizonColorARGB;
        std::atomic<int> _groundColorARGB;
        std::atomic<float> _horizonBlend;
        std::atomic<bool> _sunDiscEnabled;

        std::string _shaderSource;
        mutable std::mutex _shaderSourceMutex;

        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _onChangeListenersMutex;
    };

}

#endif
