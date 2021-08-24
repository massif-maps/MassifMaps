#include "ExpressionContext.h"
#include "Expression.h"
#include "Feature.h"
#include "ValueConverter.h"

namespace carto { namespace mvt {
    void ExpressionContext::setTileId(const vt::TileId& tileId) {
        _tileId = tileId;
    }
    
    void ExpressionContext::setAdjustedZoom(int zoom) {
        _adjustedZoom = zoom;
        _scaleDenom = zoom2ScaleDenominator(static_cast<float>(_adjustedZoom));
    }

    void ExpressionContext::setScaleDenominator(float scaleDenom) {
        _scaleDenom = scaleDenom;
        _adjustedZoom = static_cast<int>(scaleDenominator2Zoom(_scaleDenom));
    }

    Value ExpressionContext::getVariable(const std::string& name) const {
        if (isViewStateVariable(name)) {
            if (name == "view::zoom") {
                return Value(static_cast<double>(_adjustedZoom + 0.5)); // use 'average' zoom; this is only needed for expressions that are not evaluated at view-time
            }
            return Value();
        }
        else if (isMapnikVariable(name)) {
            if (name == "mapnik::geometry_type") {
                return Value(static_cast<long long>(_featureData->getGeometryType()));
            }
            return Value();
        }
        else if (isNutiVariable(name)) {
            if (_nutiParameterValueMap) {
                auto it = _nutiParameterValueMap->find(name.substr(6));
                if (it != _nutiParameterValueMap->end()) {
                    return it->second;
                }
            }
            return Value();
        }
        else if (isZoomVariable(name)) {
            return Value(static_cast<long long>(_adjustedZoom));
        }

        if (_featureData) {
            Value value;
            if (_featureData->getVariable(name, value)) {
                return value;
            }
        }
        return Value();
    }

    Value ExpressionContext::getViewStateVariable(const vt::ViewState& viewState, const std::string& name) const {
        if (isViewStateVariable(name)) {
            if (name == "view::zoom") {
                return viewState.zoom;
            }
            return Value();
        }
        return getVariable(name);
    }
} }
