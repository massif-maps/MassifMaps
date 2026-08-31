/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_MAPSETTINGSTABLE_H_
#define _MASSIF_MAPNIKVT_MAPSETTINGSTABLE_H_

#include "Map.h"

#include <array>
#include <utility>

namespace massif::mvt {
    /**
     * The Map-block settings that are plain style properties (sun, shadows, fog, buildings,
     * terrain), under the names the CartoCSS Map block uses - see
     * CartoCSSMapLoader::loadMapSettings. The XML parser and the XML generator both walk these
     * tables, so a new setting reaches css2xml and back by being added here once.
     */
    inline constexpr std::array<std::pair<const char*, FloatFunctionProperty Map::Settings::*>, 37> MAP_SETTINGS_FLOAT_PROPERTIES = {{
        { "sun-azimuth", &Map::Settings::sunAzimuth },
        { "sun-altitude", &Map::Settings::sunAltitude },
        { "sun-intensity", &Map::Settings::sunIntensity },
        { "ambient-intensity", &Map::Settings::ambientIntensity },
        { "building-light-intensity", &Map::Settings::buildingLightIntensity },
        { "building-ambient", &Map::Settings::buildingAmbient },
        { "building-vertical-gradient", &Map::Settings::buildingVerticalGradient },
        { "building-vertical-gradient-height", &Map::Settings::buildingVerticalGradientHeight },
        { "building-ao-ground-radius", &Map::Settings::buildingAoGroundRadius },
        { "building-ao-ground-step", &Map::Settings::buildingAoGroundStep },
        { "building-ao-ground-attenuation", &Map::Settings::buildingAoGroundAttenuation },
        { "building-ao-intensity", &Map::Settings::buildingAoIntensity },
        { "text-occlusion-opacity", &Map::Settings::textOcclusionOpacity },
        { "building-edge-radius", &Map::Settings::buildingEdgeRadius },
        { "building-roof-shade", &Map::Settings::buildingRoofShade },
        { "building-rounded-roof", &Map::Settings::buildingRoundedRoof },
        { "building-height-scale", &Map::Settings::buildingHeightScale },
        { "building-height-view-scale", &Map::Settings::buildingHeightViewScale },
        { "building-grow-on-appear", &Map::Settings::buildingGrowOnAppear },
        { "building-fade-on-appear", &Map::Settings::buildingFadeOnAppear },
        { "terrain-lighting", &Map::Settings::terrainLighting },
        { "colors-prelit", &Map::Settings::colorsPrelit },
        { "building-emissive-strength", &Map::Settings::buildingEmissive },
        { "background-emissive-strength", &Map::Settings::backgroundEmissive },
        { "shadow-strength", &Map::Settings::shadowStrength },
        { "shadow-bias", &Map::Settings::shadowBias },
        { "shadow-softness", &Map::Settings::shadowSoftness },
        { "shadow-distance", &Map::Settings::shadowDistance },
        { "shadow-map-size", &Map::Settings::shadowMapSize },
        { "shadow-cascades", &Map::Settings::shadowCascades },
        { "shadow-caster-margin", &Map::Settings::shadowCasterMargin },
        { "fog-enabled", &Map::Settings::fogEnabled },
        { "fog-range-start", &Map::Settings::fogRangeStart },
        { "fog-range-end", &Map::Settings::fogRangeEnd },
        { "fog-horizon-blend", &Map::Settings::fogHorizonBlend },
        { "fog-star-intensity", &Map::Settings::fogStarIntensity },
        { "terrain-max-visible-distance", &Map::Settings::terrainMaxVisibleDistance }
    }};

    inline constexpr std::array<std::pair<const char*, ColorFunctionProperty Map::Settings::*>, 5> MAP_SETTINGS_COLOR_PROPERTIES = {{
        { "sun-color", &Map::Settings::sunColor },
        { "ambient-color", &Map::Settings::ambientColor },
        { "fog-color", &Map::Settings::fogColor },
        { "fog-high-color", &Map::Settings::fogHighColor },
        { "fog-space-color", &Map::Settings::fogSpaceColor }
    }};
}

#endif
