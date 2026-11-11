/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_MAP_H_
#define _MASSIF_MAPNIKVT_MAP_H_

#include "FontSet.h"
#include "Layer.h"
#include "Style.h"
#include "Parameter.h"
#include "StyleParameter.h"
#include "Properties.h"
#include "SelectionParameter.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

namespace massif::mvt {
    class Map {
    public:
        struct Settings {
            std::string backgroundImage;
            ColorFunctionProperty backgroundColor = ColorFunctionProperty("transparent");
            ColorFunctionProperty northPoleColor = ColorFunctionProperty("transparent");
            ColorFunctionProperty southPoleColor = ColorFunctionProperty("transparent");
            std::string fontDirectory = "fonts";
            float bufferSize = -1.0f;
            // The Options::TileDrawSize the style's zoom numbers are written for; 0 = whatever the app sets.
            float tileDrawSize = 0.0f;

            // Sun, shadows, fog and the terrain view distance: normal style properties (constant or zoom expression);
            // isDefined() says whether the style set it, unset leaves the application's own setting.
            FloatFunctionProperty sunAzimuth = FloatFunctionProperty(315.0f);      // degrees from north, clockwise
            FloatFunctionProperty sunAltitude = FloatFunctionProperty(45.0f);      // degrees above the horizon
            ColorFunctionProperty sunColor = ColorFunctionProperty("#ffffff");
            FloatFunctionProperty sunIntensity = FloatFunctionProperty(1.0f);
            FloatFunctionProperty ambientIntensity = FloatFunctionProperty(0.35f);
            ColorFunctionProperty ambientColor = ColorFunctionProperty("#ffffff"); // the sky's tint in shadow
            // Building lighting: unset follows the sun, on the terrain's normalised-Lambert model; these two tune
            // the walls alone, without moving the sun that lights the ground.
            FloatFunctionProperty buildingLightIntensity = FloatFunctionProperty(1.0f);
            FloatFunctionProperty buildingAmbient = FloatFunctionProperty(0.35f);
            // How dark the foot of a wall goes (0 = flat facade), and over how many metres of
            // absolute height it fades out - shared by every part of one building.
            FloatFunctionProperty buildingVerticalGradient = FloatFunctionProperty(0.65f);
            FloatFunctionProperty buildingVerticalGradientHeight = FloatFunctionProperty(20.0f);
            // Contact shadow on the GROUND around a footprint, on the mapbox names. ON by default, as an extrusion
            // without one reads as pasted on; 0 radius turns it off.
            FloatFunctionProperty buildingAoGroundRadius = FloatFunctionProperty(4.0f);      // metres
            FloatFunctionProperty buildingAoGroundAttenuation = FloatFunctionProperty(1.75f); // exponent of (1 - d)
            // Metres between subdivisions ALONG a wall. 0 = the terrain grid's own cell, which is
            // what the shadow has to follow; raise it to see the chord artifact, lower it to kill it.
            FloatFunctionProperty buildingAoGroundStep = FloatFunctionProperty(0.0f);         // metres, 0 = auto
            FloatFunctionProperty buildingAoIntensity = FloatFunctionProperty(0.2f);
            // The opacity a label keeps while its ANCHOR is hidden by 3D content (mapbox's
            // text-occlusion-opacity). 1 = no occlusion, and the pass that answers it does not run.
            FloatFunctionProperty textOcclusionOpacity = FloatFunctionProperty(1.0f);
            // Bevel between a wall and the roof, rounding the hard 90 degrees. 0 = off.
            FloatFunctionProperty buildingEdgeRadius = FloatFunctionProperty(0.0f); // metres
            // Roofs multiplied by this. Physically the brightest face, but darkening it is what makes a block
            // read as 3D (mapbox's look): a styling choice, not a lighting one.
            FloatFunctionProperty buildingRoofShade = FloatFunctionProperty(1.0f);
            // Every extrusion's height, multiplied: mapbox's `fill-extrusion-vertical-scale`, and how Standard grows buildings
            // out of the ground (0 at z15, 1 at z15.3) - a zoom ramp, not a timed animation.
            FloatFunctionProperty buildingHeightScale = FloatFunctionProperty(1.0f);
            // The same, but a CAMERA effect the shadow caster ignores (flattened buildings are still there).
            // Anything meaning "not there yet" belongs in buildingHeightScale, which the caster follows.
            FloatFunctionProperty buildingHeightViewScale = FloatFunctionProperty(1.0f);
            // Whether a tile's fade-in also RAISES its buildings (they used to scale with the tile blend). A style
            // wanting that writes a zoom ramp on buildingHeightScale, as mapbox does.
            FloatFunctionProperty buildingGrowOnAppear = FloatFunctionProperty(0.0f);
            // Whether a tile's fade-in also fades its buildings IN, like every other geometry; a style ramping its
            // own extrusion opacity over zoom turns it off.
            FloatFunctionProperty buildingFadeOnAppear = FloatFunctionProperty(1.0f);
            // 0 makes the bevel a flat facet with its own tone instead of a rolled edge.
            FloatFunctionProperty buildingRoundedRoof = FloatFunctionProperty(1.0f);
            // 0/1: also round the vertical corners, where that reaches a pixel (08-lighting-sky-fog.md).
            FloatFunctionProperty buildingEdgeCorners = FloatFunctionProperty(0.0f);
            FloatFunctionProperty terrainLighting = FloatFunctionProperty(0.0f);   // 0/1: light the terrain with the sun
            // 0/1: the style's 2D colours ALREADY carry the scene light (a converted MapBox style folds the ground radiance
            // in), so the ground must not be lit twice. Says nothing about terrain shadows, which stay on.
            FloatFunctionProperty colorsPrelit = FloatFunctionProperty(0.0f);
            // Share of an extrusion's colour EMITTED rather than lit (mapbox fill-extrusion-emissive-strength): 0 follows
            // the scene light, 1 is drawn as authored. Above 1 it overdrives, as their mix() does.
            FloatFunctionProperty buildingEmissive = FloatFunctionProperty(0.0f);
            // The same question for the map's BACKGROUND, which is a Map setting rather than a
            // symbolizer: mapbox's background-emissive-strength. 1 draws it as authored.
            FloatFunctionProperty backgroundEmissive = FloatFunctionProperty(1.0f);
            FloatFunctionProperty shadowStrength = FloatFunctionProperty(0.0f);    // 0 = no shadows
            FloatFunctionProperty shadowBias = FloatFunctionProperty(0.25f);       // meters
            FloatFunctionProperty shadowSoftness = FloatFunctionProperty(1.0f);    // PCF radius in shadow texels
            FloatFunctionProperty shadowDistance = FloatFunctionProperty(0.0f);    // multiples of the camera-to-focus distance, 0 = built-in 4.5
            FloatFunctionProperty shadowMapSize = FloatFunctionProperty(1024.0f);  // pixels, per cascade
            FloatFunctionProperty shadowCascades = FloatFunctionProperty(3.0f);
            FloatFunctionProperty shadowCasterMargin = FloatFunctionProperty(1.0f); // tiles beyond the visible ones
            // Fog, on the Mapbox model: the range is in multiples of the camera-to-focus
            // distance, so one setting holds at every zoom.
            FloatFunctionProperty fogEnabled = FloatFunctionProperty(1.0f);        // 0/1: the switch, wins over the app
            ColorFunctionProperty fogColor = ColorFunctionProperty("transparent");  // transparent = no fog
            FloatFunctionProperty fogRangeStart = FloatFunctionProperty(0.8f);
            FloatFunctionProperty fogRangeEnd = FloatFunctionProperty(8.0f);
            ColorFunctionProperty fogHighColor = ColorFunctionProperty("transparent");  // the upper atmosphere
            ColorFunctionProperty fogSpaceColor = ColorFunctionProperty("transparent"); // the zenith
            FloatFunctionProperty fogHorizonBlend = FloatFunctionProperty(12.0f / 90.0f); // fraction of a quarter turn
            FloatFunctionProperty fogStarIntensity = FloatFunctionProperty(0.0f);
            // Mapbox vertical-range: the fog fades out between these two altitudes, so a summit
            // stands clear of a haze filling the valley. Equal values disable the fade.
            FloatFunctionProperty fogVerticalRangeStart = FloatFunctionProperty(0.0f); // metres
            FloatFunctionProperty fogVerticalRangeEnd = FloatFunctionProperty(0.0f);
            // The sky: 0 = the two-colour gradient, 1 = Rayleigh/Mie scattering.
            FloatFunctionProperty skyType = FloatFunctionProperty(1.0f);
            FloatFunctionProperty skyAtmosphereSunIntensity = FloatFunctionProperty(10.0f);
            ColorFunctionProperty skyAtmosphereColor = ColorFunctionProperty("white"); // Rayleigh tint
            ColorFunctionProperty skyAtmosphereHaloColor = ColorFunctionProperty("white"); // Mie tint
            FloatFunctionProperty skyAtmosphereLuminance = FloatFunctionProperty(1.0f);
            FloatFunctionProperty terrainMaxVisibleDistance = FloatFunctionProperty(0.0f); // meters, 0 = unlimited

            // Levels to add to the app's zoom so the style reads the zoom it was written for: the same
            // ground is a level higher at 256 than at 512.
            float zoomShift(float appTileDrawSize) const {
                return tileDrawSize > 0.0f && appTileDrawSize > 0.0f ? std::log2(appTileDrawSize / tileDrawSize) : 0.0f;
            }
        };
        
        explicit Map(const Settings& settings) : _settings(settings) { }
        virtual ~Map() = default;

        const Settings& getSettings() const { return _settings; }

        void setStyleParameters(const std::vector<StyleParameter>& styleParameters);
        const std::map<std::string, StyleParameter>& getStyleParameterMap() const { return _styleParameterMap; }

        // The parameter this style selects features with, once resolveSelectionParameter has looked
        // for one. Unset means every parameter change is a re-decode, as it always was.
        void setSelectionParameter(std::optional<SelectionParameter> selectionParameter) { _selectionParameter = std::move(selectionParameter); }
        const std::optional<SelectionParameter>& getSelectionParameter() const { return _selectionParameter; }
        
        void setParameters(const std::vector<Parameter>& parameters);
        const std::map<std::string, Parameter>& getParameterMap() const { return _parameterMap; }

        void clearStyles();
        void addStyle(const std::shared_ptr<Style>& style);
        const std::shared_ptr<Style>& getStyle(const std::string& name) const;
        const std::vector<std::shared_ptr<Style>>& getStyles() const { return _styles; }

        void clearFontSets();
        void addFontSet(const std::shared_ptr<FontSet>& fontSet);
        const std::shared_ptr<FontSet>& getFontSet(const std::string& name) const;
        const std::vector<std::shared_ptr<FontSet>>& getFontSets() const { return _fontSets; }

        void clearLayers();
        void addLayer(const std::shared_ptr<Layer>& layer);
        const std::shared_ptr<Layer>& getLayer(const std::string& name) const;
        const std::vector<std::shared_ptr<Layer>>& getLayers() const { return _layers; }

    private:
        Settings _settings;
        std::map<std::string, StyleParameter> _styleParameterMap;
        std::optional<SelectionParameter> _selectionParameter;
        std::map<std::string, Parameter> _parameterMap;
        std::vector<std::shared_ptr<Style>> _styles;
        std::unordered_map<std::string, std::shared_ptr<Style>> _styleMap;
        std::vector<std::shared_ptr<FontSet>> _fontSets;
        std::map<std::string, std::shared_ptr<FontSet>> _fontSetMap;
        std::vector<std::shared_ptr<Layer>> _layers;
        std::map<std::string, std::shared_ptr<Layer>> _layerMap;
    };
}

#endif
