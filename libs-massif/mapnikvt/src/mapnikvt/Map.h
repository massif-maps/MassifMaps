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

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <map>

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

            // Sun, shadows, fog and the terrain view distance. Every one of them is a normal
            // style property: it may be a constant or any zoom-dependent expression, linear()
            // included, and isDefined() says whether the style set it at all - unset means the
            // application's own setting stands.
            FloatFunctionProperty sunAzimuth = FloatFunctionProperty(315.0f);      // degrees from north, clockwise
            FloatFunctionProperty sunAltitude = FloatFunctionProperty(45.0f);      // degrees above the horizon
            ColorFunctionProperty sunColor = ColorFunctionProperty("#ffffff");
            FloatFunctionProperty sunIntensity = FloatFunctionProperty(1.0f);
            FloatFunctionProperty ambientIntensity = FloatFunctionProperty(0.35f);
            // Building (3D extrusion) lighting. Unset means the extrusions follow the terrain sun
            // when terrain lighting is on, and the legacy view-direction shading when it is off -
            // which is why walls change darkness as terrain lighting is toggled. Setting either of
            // these puts the extrusions on the normalised-Lambert model whatever the terrain does.
            FloatFunctionProperty buildingLightIntensity = FloatFunctionProperty(1.0f);
            FloatFunctionProperty buildingAmbient = FloatFunctionProperty(0.35f);
            FloatFunctionProperty terrainLighting = FloatFunctionProperty(0.0f);   // 0/1: light the terrain with the sun
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
            FloatFunctionProperty terrainMaxVisibleDistance = FloatFunctionProperty(0.0f); // meters, 0 = unlimited
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
        std::map<std::string, std::shared_ptr<Style>> _styleMap;
        std::vector<std::shared_ptr<FontSet>> _fontSets;
        std::map<std::string, std::shared_ptr<FontSet>> _fontSetMap;
        std::vector<std::shared_ptr<Layer>> _layers;
        std::map<std::string, std::shared_ptr<Layer>> _layerMap;
    };
}

#endif
