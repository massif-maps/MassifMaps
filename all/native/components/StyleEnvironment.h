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
     * The sun, shadow, fog and terrain-distance values a vector tile style's Map block provides, evaluated
     * per frame (so zoom-dependent). Unset means the app's LightOptions/TerrainOptions value stands.
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
        std::optional<float> buildingHeightScale;
        std::optional<float> buildingHeightViewScale;
        std::optional<bool> buildingGrowOnAppear;
        std::optional<bool> buildingFadeOnAppear;
        std::optional<float> buildingAoIntensity;
        std::optional<float> textOcclusionOpacity;
        std::optional<float> buildingAoGroundAttenuation;
        std::optional<bool> terrainLightingEnabled;
        // The style says its 2D colours already carry the light - see Map::Settings::colorsPrelit.
        std::optional<bool> colorsPrelit;
        std::optional<float> buildingEmissive;
        std::optional<float> backgroundEmissive;
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
         * Takes over every value the other environment defines and this one does not:
         * across layers, the first one to set a property wins.
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
        // Set by a style whose 2D colours are pre-lit: the ground is then drawn as authored while
        // the terrain's shadows and the 3D pass carry on lighting normally.
        bool colorsPrelit = false;
        // mapbox's fill-extrusion-emissive-strength: the share of the colour emitted rather than lit.
        float buildingEmissive = 0.0f;
        // The same for the map background (a Map setting, not a symbolizer). 1 = drawn as authored.
        float backgroundEmissive = 1.0f;
        // mapbox's ["measure-light", "brightness"], 0-1 - what a style's `view::brightness` reads.
        float brightness = 1.0f;
        // mapbox's calculateGroundRadiance for an upward-facing surface, in linear space; the colour grade's input.
        cglib::vec3<float> radiance = cglib::vec3<float>(1.0f, 1.0f, 1.0f);
        cglib::vec3<float> sunDir = cglib::vec3<float>(0, 0, 1);
        Color sunColor = Color(255, 255, 255, 255);
        float sunIntensity = 1.0f;
        float ambientIntensity = 0.35f;
        Color ambientColor = Color(255, 255, 255, 255);
        // mapbox's fill-extrusion model, summed in linear space: 0.5 + 0.5 = 1 in full sun.
        // The ambient is the walls' own, so flattening the ground does not flatten the facades.
        float buildingLightIntensity = 0.5f;
        float buildingAmbient = 0.5f;
        // Wall-foot darkening as a fraction of its colour; off, as mapbox has no facade gradient.
        // Its reach is decode-time geometry (TileLayerBuilder::packGradientT).
        float buildingVerticalGradient = 0.0f;
        float buildingRoofShade = 1.0f;
        // maplibre's fill-extrusion model, used when nothing (style, options, day cycle) lights the map,
        // as that is what a plain converted style's author saw. See TileRenderer::LIGHTING_SHADER_3D.
        bool buildingLightingMapLibre = false;
        // Every extrusion's height, multiplied - mapbox's fill-extrusion-vertical-scale.
        float buildingHeightScale = 1.0f;
        // Drawn only - the shadow caster ignores it (see mvt::Map::Settings).
        float buildingHeightViewScale = 1.0f;
        // Whether a tile's fade-in raises its buildings; off, as no source style asks for it.
        bool buildingGrowOnAppear = false;
        // Off: a half-transparent wall would show its own shadow, drawn at full strength from the first frame.
        bool buildingFadeOnAppear = false;
        // Shade the ground contact skirt; its radius is decode-time geometry (TileLayerBuilder::appendGroundSkirt).
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
     * The distance fog to render with: FogOptions with the style's values substituted in, lit by the sun
     * when terrain lighting is on. Distances are internal units; range* keep the API's camera-distance multiples.
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
        // Set even when the fog is off, so switching it off does not force a shader rebuild.
        std::string shaderSource;

        /**
         * True when there is a fog to draw at all: a visible colour over a positive range.
         */
        bool active() const { return color.getA() > 0 && distance > startDistance; }
    };

    /**
     * Resolves the fog and, with terrain lighting on, lights it so a daylight fog darkens at night.
     * cameraDistance (ViewState::calculateCameraDistance, internal units) scales the range;
     * tilt fades the fog out towards top-down (FogPitchFade.h).
     */
    ResolvedFog resolveFog(const std::shared_ptr<FogOptions>& fogOptions, const StyleEnvironment& env, const ResolvedLighting& lighting, double cameraDistance, float tilt);

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
