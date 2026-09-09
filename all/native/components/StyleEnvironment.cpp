#include "StyleEnvironment.h"
#include "components/DayCycleLight.h"
#include "components/TerrainOptions.h"
#include "components/LightOptions.h"
#include "components/FogOptions.h"
#include "components/SkyOptions.h"
#include "utils/Const.h"

#include <algorithm>
#include <cmath>

namespace massif {

    void StyleEnvironment::mergeMissing(const StyleEnvironment& other) {
        auto take = [](auto& value, const auto& otherValue) {
            if (!value) {
                value = otherValue;
            }
        };
        take(sunAzimuth, other.sunAzimuth);
        take(sunAltitude, other.sunAltitude);
        take(sunColor, other.sunColor);
        take(sunIntensity, other.sunIntensity);
        take(ambientIntensity, other.ambientIntensity);
        take(ambientColor, other.ambientColor);
        take(buildingLightIntensity, other.buildingLightIntensity);
        take(buildingAmbient, other.buildingAmbient);
        take(buildingVerticalGradient, other.buildingVerticalGradient);
        take(buildingRoofShade, other.buildingRoofShade);
        take(buildingHeightScale, other.buildingHeightScale);
        take(buildingHeightViewScale, other.buildingHeightViewScale);
        take(buildingGrowOnAppear, other.buildingGrowOnAppear);
        take(buildingFadeOnAppear, other.buildingFadeOnAppear);
        take(buildingAoIntensity, other.buildingAoIntensity);
        take(textOcclusionOpacity, other.textOcclusionOpacity);
        take(buildingAoGroundAttenuation, other.buildingAoGroundAttenuation);
        take(terrainLightingEnabled, other.terrainLightingEnabled);
        take(colorsPrelit, other.colorsPrelit);
        take(buildingEmissive, other.buildingEmissive);
        take(backgroundEmissive, other.backgroundEmissive);
        take(shadowStrength, other.shadowStrength);
        take(shadowBias, other.shadowBias);
        take(shadowSoftness, other.shadowSoftness);
        take(shadowDistance, other.shadowDistance);
        take(shadowMapSize, other.shadowMapSize);
        take(shadowCascades, other.shadowCascades);
        take(shadowCasterMargin, other.shadowCasterMargin);
        take(fogEnabled, other.fogEnabled);
        take(fogColor, other.fogColor);
        take(fogRangeStart, other.fogRangeStart);
        take(fogRangeEnd, other.fogRangeEnd);
        take(fogHighColor, other.fogHighColor);
        take(fogSpaceColor, other.fogSpaceColor);
        take(fogHorizonBlend, other.fogHorizonBlend);
        take(fogVerticalRangeStart, other.fogVerticalRangeStart);
        take(fogVerticalRangeEnd, other.fogVerticalRangeEnd);
        take(fogStarIntensity, other.fogStarIntensity);
        take(skyType, other.skyType);
        take(skyAtmosphereSunIntensity, other.skyAtmosphereSunIntensity);
        take(skyAtmosphereColor, other.skyAtmosphereColor);
        take(skyAtmosphereHaloColor, other.skyAtmosphereHaloColor);
        take(skyAtmosphereLuminance, other.skyAtmosphereLuminance);
        take(terrainMaxVisibleDistance, other.terrainMaxVisibleDistance);
    }

    bool StyleEnvironment::empty() const {
        return !(sunAzimuth || sunAltitude || sunColor || sunIntensity || ambientIntensity || ambientColor || buildingLightIntensity || buildingAmbient || buildingVerticalGradient || buildingRoofShade || buildingHeightScale || buildingGrowOnAppear || buildingFadeOnAppear || buildingAoIntensity || textOcclusionOpacity || buildingAoGroundAttenuation || terrainLightingEnabled ||
                 shadowStrength || shadowBias || shadowSoftness || shadowDistance || shadowMapSize || shadowCascades ||
                 shadowCasterMargin || fogEnabled || fogColor || fogRangeStart || fogRangeEnd || fogHighColor || fogSpaceColor ||
                 fogHorizonBlend || fogVerticalRangeStart || fogVerticalRangeEnd || fogStarIntensity ||
                 skyType || skyAtmosphereSunIntensity || skyAtmosphereColor || skyAtmosphereHaloColor || skyAtmosphereLuminance ||
                 terrainMaxVisibleDistance);
    }

namespace {
    /** An app-supplied curve, read through the same interpolation the built-in one uses. */
    DayCycleLight::Setup atSunHeight(const std::vector<LightStop>& stops, float altitudeDegrees) {
        std::vector<DayCycleLight::Stop> curve;
        curve.reserve(stops.size());
        for (const LightStop& stop : stops) {
            const Color& ambient = stop.getAmbientColor();
            const Color& sun = stop.getSunColor();
            curve.push_back({ stop.getSunAltitude(), {
                { ambient.getR() / 255.0f, ambient.getG() / 255.0f, ambient.getB() / 255.0f }, stop.getAmbientIntensity(),
                { sun.getR() / 255.0f, sun.getG() / 255.0f, sun.getB() / 255.0f }, stop.getSunIntensity() } });
        }
        return DayCycleLight::atSunHeight(curve.data(), curve.size(), altitudeDegrees);
    }

    Color colorOf(const float channels[3]) {
        auto byte = [](float c) { return static_cast<unsigned char>(std::max(0.0f, std::min(1.0f, c)) * 255.0f + 0.5f); };
        return Color(byte(channels[0]), byte(channels[1]), byte(channels[2]), 255);
    }
}

    ResolvedLighting resolveLighting(const std::shared_ptr<LightOptions>& lightOptions, const StyleEnvironment& env) {
        ResolvedLighting lighting;
        if (lightOptions) {
            lighting.terrainLightingEnabled = lightOptions->isTerrainLightingEnabled();
            lighting.sunDir = lightOptions->getSunDirection();
            lighting.sunColor = lightOptions->getSunColor();
            lighting.sunIntensity = lightOptions->getSunIntensity();
            lighting.ambientIntensity = lightOptions->getAmbientIntensity();
            lighting.ambientColor = lightOptions->getAmbientColor();
            lighting.shadowStrength = lightOptions->getShadowStrength();
            lighting.shadowBias = lightOptions->getShadowBias();
        lighting.shadowNormalOffset = lightOptions->getShadowNormalOffset();
            lighting.shadowSoftness = lightOptions->getShadowSoftness();
            lighting.shadowDistance = lightOptions->getShadowDistance();
            lighting.shadowMapSize = lightOptions->getShadowMapSize();
            lighting.shadowCascades = lightOptions->getShadowCascades();
            lighting.shadowCasterMargin = lightOptions->getShadowCasterMargin();
        }
        // The sun direction is derived from two properties, so it is rebuilt whenever the style
        // overrides either - unless the app asked to keep its own: a day/night cycle has to move the
        // sun on a style that states one, and a converted MapBox style states one per preset.
        bool appSun = lightOptions && lightOptions->isSunOverridingStyle();
        if ((env.sunAzimuth || env.sunAltitude) && !appSun) {
            double azimuth = (env.sunAzimuth ? *env.sunAzimuth : (lightOptions ? lightOptions->getSunAzimuth() : 315.0f)) * Const::DEG_TO_RAD;
            double altitude = (env.sunAltitude ? *env.sunAltitude : (lightOptions ? lightOptions->getSunAltitude() : 45.0f)) * Const::DEG_TO_RAD;
            double cosAltitude = std::cos(altitude);
            lighting.sunDir = cglib::vec3<float>(static_cast<float>(cosAltitude * std::sin(azimuth)),
                                                 static_cast<float>(cosAltitude * std::cos(azimuth)),
                                                 static_cast<float>(std::sin(altitude)));
        }
        if (env.sunColor) {
            lighting.sunColor = *env.sunColor;
        }
        if (env.sunIntensity) {
            lighting.sunIntensity = *env.sunIntensity;
        }
        if (env.ambientIntensity) {
            lighting.ambientIntensity = *env.ambientIntensity;
        }
        if (env.ambientColor) {
            lighting.ambientColor = *env.ambientColor;
        }
        if (env.terrainLightingEnabled) {
            lighting.terrainLightingEnabled = *env.terrainLightingEnabled;
        }
        if (env.colorsPrelit) {
            lighting.colorsPrelit = *env.colorsPrelit;
        }
        if (env.buildingEmissive) {
            lighting.buildingEmissive = *env.buildingEmissive;
        }
        if (env.backgroundEmissive) {
            lighting.backgroundEmissive = *env.backgroundEmissive;
        }
        // Buildings follow the sun whatever terrainLightingEnabled says - gating the walls on it too
        // gave the extrusions a second lighting model that changed shape as the terrain was toggled.
        // Only a STATED sun carries, though. mapbox's model wants the two intensities to partition
        // the light (Standard asks for 0.8 + 0.2), and LightOptions' own default is a full 1.0 -
        // summed with the walls' 0.5 ambient that put every sunlit roof past 1, where it clamped to
        // white on any style that lights nothing of its own.
        if (env.sunIntensity || (lightOptions && lightOptions->isSunIntensityStated())) {
            lighting.buildingLightIntensity = lighting.sunIntensity;
        }
        // Their AMBIENT is their own and does not follow the ground's: ambient is the floor the
        // directional term sits on, so flattening the ground with ambient 1 - normal under a
        // hillshade - would flatten every facade too. 'building-ambient' ties them back together.
        if (env.buildingLightIntensity) {
            lighting.buildingLightIntensity = *env.buildingLightIntensity;
        }
        if (env.buildingAmbient) {
            lighting.buildingAmbient = *env.buildingAmbient;
        }
        if (env.buildingVerticalGradient) {
            lighting.buildingVerticalGradient = *env.buildingVerticalGradient;
        }
        if (env.buildingRoofShade) {
            lighting.buildingRoofShade = *env.buildingRoofShade;
        }
        // A map nothing lights is a plain converted style, and what its author saw is MAPLIBRE
        // drawing it - a different fill-extrusion model, and the difference is the facades: it
        // floors the directional term at 1 - intensity whichever way a wall faces, so its walls sit
        // at 42-63% of the roof where this one cannot get below 74% without blowing the roof out at
        // some other sun altitude. The moment anything states a light, mapbox's model is the right
        // one - it is what a converted Standard and the day cycle are written against.
        lighting.buildingLightingMapLibre =
            !env.buildingLightIntensity && !env.buildingAmbient && !env.buildingVerticalGradient
            && !env.sunIntensity && !env.sunAltitude && !env.sunAzimuth && !env.ambientIntensity
            && !(lightOptions && (lightOptions->isSunIntensityStated() || lightOptions->isDayCycleLightsEnabled()));
        if (env.buildingHeightScale) {
            lighting.buildingHeightScale = *env.buildingHeightScale;
        }
        if (env.buildingHeightViewScale) {
            lighting.buildingHeightViewScale = *env.buildingHeightViewScale;
        }
        if (env.buildingGrowOnAppear) {
            lighting.buildingGrowOnAppear = *env.buildingGrowOnAppear;
        }
        if (env.buildingFadeOnAppear) {
            lighting.buildingFadeOnAppear = *env.buildingFadeOnAppear;
        }
        if (env.buildingAoIntensity) {
            lighting.buildingAoIntensity = *env.buildingAoIntensity;
        }
        if (env.buildingAoGroundAttenuation) {
            lighting.buildingAoGroundAttenuation = *env.buildingAoGroundAttenuation;
        }
        if (env.shadowStrength) {
            lighting.shadowStrength = *env.shadowStrength;
        }
        if (env.shadowBias) {
            lighting.shadowBias = *env.shadowBias;
        }
        if (env.shadowSoftness) {
            lighting.shadowSoftness = *env.shadowSoftness;
        }
        if (env.shadowDistance) {
            lighting.shadowDistance = *env.shadowDistance;
        }
        if (env.shadowMapSize) {
            lighting.shadowMapSize = *env.shadowMapSize;
        }
        if (env.shadowCascades) {
            lighting.shadowCascades = *env.shadowCascades;
        }
        if (env.shadowCasterMargin) {
            lighting.shadowCasterMargin = *env.shadowCasterMargin;
        }

        // The light COLOURS follow the sun's height when the app asked for a day cycle, replacing
        // whatever the style and the options state for them. The direction is untouched - it is the
        // input this reads.
        if (lightOptions && lightOptions->isDayCycleLightsEnabled()) {
            float altitude = std::asin(std::max(-1.0f, std::min(1.0f, lighting.sunDir(2)))) * static_cast<float>(Const::RAD_TO_DEG);
            // East of north is morning: the same height then means dawn rather than dusk.
            bool rising = lighting.sunDir(0) >= 0.0f;
            // The app's own curve when it set one - that list is the whole formula, and everything
            // below is derived from the light it returns.
            std::vector<LightStop> stops = rising ? lightOptions->getDayCycleRisingLightStops() : std::vector<LightStop>();
            if (stops.empty()) {
                stops = lightOptions->getDayCycleLightStops();
            }
            DayCycleLight::Setup light = stops.empty() ? DayCycleLight::atSunHeight(altitude, rising)
                                                       : atSunHeight(stops, altitude);
            lighting.ambientColor = colorOf(light.ambient);
            lighting.ambientIntensity = light.ambientIntensity;
            lighting.sunColor = colorOf(light.direct);
            lighting.sunIntensity = light.directIntensity;
            lighting.buildingAmbient = light.ambientIntensity;
            lighting.buildingLightIntensity = light.directIntensity;
        }

        // mapbox's calculateGroundRadiance with the ground normal: what their light does to a flat,
        // upward-facing surface, and the same number the style converter folds into a pre-lit
        // palette. Not neutralised for a pre-lit style - the grade only fires below emissive 1.
        {
            const Color& ambientColor = lighting.ambientColor;
            const Color& sunColor = lighting.sunColor;
            DayCycleLight::Setup light = {
                { ambientColor.getR() / 255.0f, ambientColor.getG() / 255.0f, ambientColor.getB() / 255.0f },
                lighting.ambientIntensity,
                { sunColor.getR() / 255.0f, sunColor.getG() / 255.0f, sunColor.getB() / 255.0f },
                lighting.sunIntensity
            };
            float radiance[3];
            DayCycleLight::groundRadiance(light, lighting.sunDir(2), radiance);
            lighting.radiance = cglib::vec3<float>(radiance[0], radiance[1], radiance[2]);
            lighting.brightness = DayCycleLight::brightness(light, lighting.sunDir(2));
            // A shadow only hides the DIRECT light, so the shaders get the strength times that
            // light's share - 1 is mapbox's shadow exactly, and 0 under the horizon skips the caster
            // pass. Clamped: the shaders read `mix(1, lit, strength)`, which a value past 1 inverts.
            lighting.shadowStrength = std::min(1.0f, lighting.shadowStrength * DayCycleLight::directShare(light, lighting.sunDir(2)));
        }
        return lighting;
    }


    float resolveTextOcclusionOpacity(const std::shared_ptr<TerrainOptions>& terrainOptions, const StyleEnvironment& env) {
        float opacity = (terrainOptions ? terrainOptions->getTextOcclusionOpacity() : 1.0f);
        if (env.textOcclusionOpacity) {
            opacity = *env.textOcclusionOpacity;
        }
        return std::min(1.0f, std::max(0.0f, opacity));
    }

    ResolvedFog resolveFog(const std::shared_ptr<FogOptions>& fogOptions, const StyleEnvironment& env, const ResolvedLighting& lighting, double cameraDistance) {
        ResolvedFog fog;
        if (!fogOptions) {
            return fog;
        }
        fog.shaderSource = fogOptions->getShaderSource();
        // ANDed rather than overridden: the style saying "fog" must not re-enable a fog the app
        // switched off. It stops the HAZE only - the atmosphere colours and the stars live on
        // FogOptions but belong to the sky, so the switch drops the fog COLOUR and range at the end.
        bool enabled = fogOptions->isEnabled() && !(env.fogEnabled && !*env.fogEnabled);
        float rangeStart = fogOptions->getRangeStart();
        float rangeEnd = fogOptions->getRangeEnd();
        fog.color = fogOptions->getColor();
        fog.highColor = fogOptions->getHighColor();
        fog.spaceColor = fogOptions->getSpaceColor();
        fog.horizonBlend = fogOptions->getHorizonBlend();
        fog.verticalRangeStart = fogOptions->getVerticalRangeStart();
        fog.verticalRangeEnd = fogOptions->getVerticalRangeEnd();
        fog.starIntensity = fogOptions->getStarIntensity();
        if (env.fogColor) {
            fog.color = *env.fogColor;
        }
        if (env.fogRangeStart) {
            rangeStart = *env.fogRangeStart;
        }
        if (env.fogRangeEnd) {
            rangeEnd = *env.fogRangeEnd;
        }
        if (env.fogHighColor) {
            fog.highColor = *env.fogHighColor;
        }
        if (env.fogSpaceColor) {
            fog.spaceColor = *env.fogSpaceColor;
        }
        if (env.fogHorizonBlend) {
            fog.horizonBlend = *env.fogHorizonBlend;
        }
        if (env.fogVerticalRangeStart) {
            fog.verticalRangeStart = *env.fogVerticalRangeStart;
        }
        if (env.fogVerticalRangeEnd) {
            fog.verticalRangeEnd = *env.fogVerticalRangeEnd;
        }
        if (env.fogStarIntensity) {
            fog.starIntensity = *env.fogStarIntensity;
        }
        // The range is in multiples of the camera-to-focus distance - a function of the zoom
        // alone, so a style tuned once holds at every zoom instead of needing an expression.
        fog.rangeStart = rangeStart;
        fog.rangeEnd = rangeEnd;
        fog.rangeScale = static_cast<float>(std::max(1.0e-9, cameraDistance));
        fog.startDistance = rangeStart * fog.rangeScale;
        fog.distance = rangeEnd * fog.rangeScale;

        // Haze is lit air: bright at noon, dark at night, the sun's colour near sunset. Scaled by
        // the same light the ground gets, so fog and terrain darken together instead of the fog
        // floating over a black map. The tint follows how much of that light is direct sun.
        if (lighting.terrainLightingEnabled && fog.color.getA() > 0) {
            float sunUp = std::max(0.0f, std::min(1.0f, lighting.sunDir(2)));
            float direct = std::max(0.0f, lighting.sunIntensity) * sunUp;
            float light = std::max(0.0f, std::min(1.0f, std::max(0.0f, lighting.ambientIntensity) + direct));
            float sunShare = direct > 0.0f ? std::min(1.0f, direct / std::max(1.0e-3f, light)) : 0.0f;
            auto channel = [&](int value, int sunValue) {
                float tint = 1.0f + sunShare * (sunValue / 255.0f - 1.0f);
                return static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, value * light * tint)));
            };
            fog.color = Color(channel(fog.color.getR(), lighting.sunColor.getR()),
                              channel(fog.color.getG(), lighting.sunColor.getG()),
                              channel(fog.color.getB(), lighting.sunColor.getB()),
                              fog.color.getA());
        }
        if (!enabled) {
            fog.color = Color(0, 0, 0, 0);
            fog.startDistance = 0.0f;
            fog.distance = 0.0f;
        }
        return fog;
    }

    ResolvedSky resolveSky(const std::shared_ptr<SkyOptions>& skyOptions, const StyleEnvironment& env) {
        ResolvedSky sky;
        if (skyOptions) {
            sky.type = skyOptions->getType();
            sky.atmosphereSunIntensity = skyOptions->getAtmosphereSunIntensity();
            sky.atmosphereColor = skyOptions->getAtmosphereColor();
            sky.haloColor = skyOptions->getHaloColor();
            sky.atmosphereLuminance = skyOptions->getAtmosphereLuminance();
        }
        if (env.skyType) {
            sky.type = *env.skyType != 0.0f ? SkyType::SKY_TYPE_ATMOSPHERE : SkyType::SKY_TYPE_GRADIENT;
        }
        if (env.skyAtmosphereSunIntensity) {
            sky.atmosphereSunIntensity = *env.skyAtmosphereSunIntensity;
        }
        if (env.skyAtmosphereColor) {
            sky.atmosphereColor = *env.skyAtmosphereColor;
        }
        if (env.skyAtmosphereHaloColor) {
            sky.haloColor = *env.skyAtmosphereHaloColor;
        }
        if (env.skyAtmosphereLuminance) {
            sky.atmosphereLuminance = *env.skyAtmosphereLuminance;
        }
        return sky;
    }

}
