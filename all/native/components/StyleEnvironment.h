/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_STYLEENVIRONMENT_H_
#define _MASSIF_STYLEENVIRONMENT_H_

#include "components/SkyOptions.h"
#include "graphics/Color.h"

#include <memory>
#include <optional>
#include <string>

#include <cglib/vec.h>

namespace massif {
    class TerrainOptions;
    class LightOptions;
    class FogOptions;

    /**
     * The sun, shadow, fog and terrain-distance values a vector tile style provides in its Map
     * block. Every field is optional: unset means the style said nothing about it and the
     * application's own LightOptions/TerrainOptions setting stands.
     *
     * The values are evaluated per frame from the style expressions, so any of them may be
     * zoom-dependent (linear(), step functions, whatever the style writes).
     *
     * Internal class, not exposed through the public API.
     */
    struct StyleEnvironment {
        std::optional<float> sunAzimuth;
        std::optional<float> sunAltitude;
        std::optional<Color> sunColor;
        std::optional<float> sunIntensity;
        std::optional<float> ambientIntensity;
        std::optional<Color> ambientColor;
        std::optional<float> buildingLightIntensity;
        std::optional<float> buildingAmbient;
        std::optional<float> buildingVerticalGradient;
        std::optional<float> buildingRoofShade;
        std::optional<float> buildingAoIntensity;
        std::optional<float> textOcclusionOpacity;
        std::optional<float> buildingAoGroundAttenuation;
        std::optional<bool> terrainLightingEnabled;
        std::optional<float> shadowStrength;
        std::optional<float> shadowBias;
        std::optional<float> shadowSoftness;
        std::optional<float> shadowDistance;
        std::optional<int> shadowMapSize;
        std::optional<int> shadowCascades;
        std::optional<int> shadowCasterMargin;
        std::optional<bool> fogEnabled;
        std::optional<Color> fogColor;
        std::optional<float> fogRangeStart;
        std::optional<float> fogRangeEnd;
        std::optional<Color> fogHighColor;
        std::optional<Color> fogSpaceColor;
        std::optional<float> fogHorizonBlend;
        std::optional<float> fogVerticalRangeStart;
        std::optional<float> fogVerticalRangeEnd;
        std::optional<float> fogStarIntensity;
        std::optional<float> skyType;
        std::optional<float> skyAtmosphereSunIntensity;
        std::optional<Color> skyAtmosphereColor;
        std::optional<Color> skyAtmosphereHaloColor;
        std::optional<float> skyAtmosphereLuminance;
        std::optional<float> terrainMaxVisibleDistance;

        /**
         * Takes over every value the other environment defines and this one does not. Used to
         * merge several layers' styles: the first layer that says something about a property wins.
         */
        void mergeMissing(const StyleEnvironment& other);

        bool empty() const;
    };

    /**
     * The lighting to actually render with: the application's LightOptions, with every value the
     * style defines substituted in.
     */
    struct ResolvedLighting {
        bool terrainLightingEnabled = false;
        cglib::vec3<float> sunDir = cglib::vec3<float>(0, 0, 1);
        Color sunColor = Color(255, 255, 255, 255);
        float sunIntensity = 1.0f;
        float ambientIntensity = 0.35f;
        Color ambientColor = Color(255, 255, 255, 255);
        // What the 3D extrusions light with: mapbox's fill-extrusion model, summed in linear space
        // (TileRenderer::LIGHTING_SHADER_3D). Both default to their 0.5, which sums to exactly 1
        // in full sun - a facade the light reaches keeps its own colour, whatever the hour.
        // The ambient is the walls' own, so flattening the ground does not flatten every facade
        // with it (see resolveLighting).
        float buildingLightIntensity = 0.5f;
        float buildingAmbient = 0.5f;
        // How dark the foot of a wall goes, as a fraction of its colour. Off by default: mapbox has
        // no facade gradient, the direction-aware ambient separates the walls instead. The reach it
        // fades over is decode-time geometry, not a uniform - see TileLayerBuilder::appendWallQuad.
        float buildingVerticalGradient = 0.0f;
        float buildingRoofShade = 1.0f;
        // The contact shadow on the ground around a footprint. Its RADIUS is decode-time geometry
        // (TileLayerBuilder::appendGroundSkirt); these two shade the skirt it produced.
        float buildingAoIntensity = 0.2f;
        float buildingAoGroundAttenuation = 1.75f;
        float shadowStrength = 0.0f;
        float shadowBias = 0.25f;
        float shadowNormalOffset = 3.0f;
        float shadowSoftness = 1.0f;
        float shadowDistance = 0.0f;
        int shadowMapSize = 1024;
        int shadowCascades = 3;
        int shadowCasterMargin = 3;
    };

    ResolvedLighting resolveLighting(const std::shared_ptr<LightOptions>& lightOptions, const StyleEnvironment& env);

    /**
     * The opacity a label keeps while its anchor is hidden by 3D content: TerrainOptions'
     * TextOcclusionOpacity, or the style's 'text-occlusion-opacity' where it sets one. 1 means no
     * occlusion at all, and the pass that answers it is skipped.
     */
    float resolveTextOcclusionOpacity(const std::shared_ptr<TerrainOptions>& terrainOptions, const StyleEnvironment& env);

    /**
     * The distance fog to actually render with: FogOptions, with every value the style defines
     * substituted in, and the colour lit by the sun when terrain lighting is on.
     * The API expresses the range in multiples of the camera-to-focus distance; the distances
     * here are the resolved product, in INTERNAL units, which is what every shader wants.
     */
    struct ResolvedFog {
        Color color = Color(0, 0, 0, 0);
        Color highColor = Color(0, 0, 0, 0);
        Color spaceColor = Color(0, 0, 0, 0);
        float rangeStart = 0.0f; // multiples of the camera-to-focus distance, as the API states it
        float rangeEnd = 0.0f;
        float rangeScale = 1.0f; // internal units per range unit
        float startDistance = 0.0f; // rangeStart * rangeScale, i.e. internal units
        float distance = 0.0f;
        float horizonBlend = 0.0f;
        // Metres. The fog fades out between the two, so a summit stands clear of a valley haze.
        float verticalRangeStart = 0.0f;
        float verticalRangeEnd = 0.0f;
        float starIntensity = 0.0f;
        // FogOptions::getShaderSource, carried here so a renderer that compiles the fog into its
        // own program does not have to reach for the options a second time. Set even when the fog
        // is off, so switching it off does not force a shader rebuild.
        std::string shaderSource;

        /**
         * True when there is a fog to draw at all: a visible colour over a positive range.
         */
        bool active() const { return color.getA() > 0 && distance > startDistance; }
    };

    /**
     * Resolves the fog and lights it: fog is air, so it is as bright as the light falling on it.
     * Without this a fog tuned for daylight stays bright white through the night, floating over a
     * dark map. Only applied when terrain lighting is on - otherwise there is no sun to speak of
     * and the configured colour is used as-is.
     *
     * cameraDistance is ViewState::calculateCameraDistance() in internal units, which the range is
     * measured in. It is a function of the zoom alone, so one range setting holds at every zoom.
     */
    ResolvedFog resolveFog(const std::shared_ptr<FogOptions>& fogOptions, const StyleEnvironment& env, const ResolvedLighting& lighting, double cameraDistance);

    /**
     * The sky to actually draw: SkyOptions, with every value the style defines substituted in.
     * The gradient colours are not style-driven and stay on SkyOptions.
     */
    struct ResolvedSky {
        SkyType::SkyType type = SkyType::SKY_TYPE_ATMOSPHERE;
        float atmosphereSunIntensity = 10.0f;
        Color atmosphereColor = Color(255, 255, 255, 255);
        Color haloColor = Color(255, 255, 255, 255);
        float atmosphereLuminance = 1.0f;
    };

    ResolvedSky resolveSky(const std::shared_ptr<SkyOptions>& skyOptions, const StyleEnvironment& env);

}

#endif
