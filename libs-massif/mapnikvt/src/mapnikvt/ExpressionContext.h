/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_EXPRESSIONCONTEXT_H_
#define _MASSIF_MAPNIKVT_EXPRESSIONCONTEXT_H_

#include "Value.h"
#include "StyleParameterStore.h"
#include "ScaleUtils.h"
#include "vt/TileId.h"
#include "vt/ViewState.h"

#include <map>
#include <memory>

namespace massif::mvt {
    class FeatureData;

    class ExpressionContext {
    public:
        ExpressionContext() = default;

        void setTileId(const vt::TileId& tileId);
        vt::TileId getTileId() const { return _tileId; }

        void setAdjustedZoom(int zoom);
        int getAdjustedZoom() const { return _adjustedZoom; }

        void setScaleDenominator(float scaleDenom);
        float getScaleDenominator() const { return _scaleDenom; }

        void setFeatureData(std::shared_ptr<const FeatureData> featureData) { _featureData = std::move(featureData); }
        const std::shared_ptr<const FeatureData>& getFeatureData() const { return _featureData; }

        void setStyleParameterStore(std::shared_ptr<const StyleParameterStore> paramStore) { _styleParameterStore = std::move(paramStore); }
        const std::shared_ptr<const StyleParameterStore>& getStyleParameterStore() const { return _styleParameterStore; }

        // While the selecting parameter is folded both ways, it reads as the value forced on it
        // rather than as the one the store holds - see resolveSelectionParameter. Held by value:
        // a property that is not foldable copies the context into a per-frame function.
        void setStyleParameterOverride(const std::string& name, const Value& value) { _styleParamOverrideName = name; _styleParamOverrideValue = value; _styleParamOverride = true; }
        void clearStyleParameterOverride() { _styleParamOverride = false; }
        bool hasStyleParameterOverride() const { return _styleParamOverride; }

        Value getVariable(const std::string& name) const;
        Value getViewStateVariable(const vt::ViewState& viewState, const std::string& name) const;

        static bool isViewStateVariable(const std::string& name) { return name.compare(0, 6, "view::") == 0; }
        static bool isMapnikVariable(const std::string& name) { return name.compare(0, 8, "mapnik::") == 0; }
        static bool isZoomVariable(const std::string& name) { return name == "zoom"; }

        // Length of the style-parameter prefix, 0 if the name is not one. "nuti::" is the
        // pre-rebrand spelling, still accepted; returning the length keeps both allocation-free.
        static std::size_t styleParameterPrefixLen(const std::string& name) {
            if (name.compare(0, 7, "param::") == 0) {
                return 7;
            }
            if (name.compare(0, 6, "nuti::") == 0) {
                return 6;
            }
            return 0;
        }
        static bool isStyleParameterVariable(const std::string& name) { return styleParameterPrefixLen(name) != 0; }

    private:
        vt::TileId _tileId = vt::TileId(0, 0, 0);
        int _adjustedZoom = 0;
        float _scaleDenom = zoom2ScaleDenominator(0);
        std::shared_ptr<const FeatureData> _featureData;
        std::shared_ptr<const StyleParameterStore> _styleParameterStore;
        bool _styleParamOverride = false;
        std::string _styleParamOverrideName;
        Value _styleParamOverrideValue;
    };
}

#endif
