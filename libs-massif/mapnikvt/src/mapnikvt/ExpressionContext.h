/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_EXPRESSIONCONTEXT_H_
#define _CARTO_MAPNIKVT_EXPRESSIONCONTEXT_H_

#include "Value.h"
#include "ScaleUtils.h"
#include "vt/TileId.h"
#include "vt/ViewState.h"

#include <map>
#include <memory>

namespace carto { namespace mvt {
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

        void setNutiParameterValueMap(std::shared_ptr<const std::map<std::string, Value>> paramValueMap) { _nutiParameterValueMap = std::move(paramValueMap); }
        const std::shared_ptr<const std::map<std::string, Value>>& getNutiParameterValueMap() const { return _nutiParameterValueMap; }

        Value getVariable(const std::string& name) const;
        Value getViewStateVariable(const vt::ViewState& viewState, const std::string& name) const;

        static bool isViewStateVariable(const std::string& name) { return name.compare(0, 6, "view::") == 0; }
        static bool isMapnikVariable(const std::string& name) { return name.compare(0, 8, "mapnik::") == 0; }
        static bool isNutiVariable(const std::string& name) { return name.compare(0, 6, "nuti::") == 0; }
        static bool isZoomVariable(const std::string& name) { return name == "zoom"; }

    private:
        vt::TileId _tileId = vt::TileId(0, 0, 0);
        int _adjustedZoom = 0;
        float _scaleDenom = zoom2ScaleDenominator(0);
        std::shared_ptr<const FeatureData> _featureData;
        std::shared_ptr<const std::map<std::string, Value>> _nutiParameterValueMap;
    };
} }

#endif
