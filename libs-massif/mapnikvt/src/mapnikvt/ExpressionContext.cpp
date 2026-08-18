#include "ExpressionContext.h"
#include "Expression.h"
#include "Feature.h"
#include "ValueConverter.h"

namespace massif::mvt {
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
        else if (isRenderVariable(name)) {
            if (name == "render::3d") {
                return Value(_render3D);
            }
            return Value();
        }
        else if (isMapnikVariable(name)) {
            if (name == "mapnik::geometry_type") {
                return Value(static_cast<long long>(_featureData->getGeometryType()));
            }
            if (name == "mapnik::feature_id") {
                return Value(_featureData->getId());
            }
            return Value();
        }
        else if (std::size_t prefixLen = styleParameterPrefixLen(name)) {
            if (_styleParamOverride && name.compare(prefixLen, std::string::npos, _styleParamOverrideName) == 0) {
                return _styleParamOverrideValue;
            }
            if (_styleParameterStore) {
                std::shared_ptr<const std::map<std::string, Value>> values = _styleParameterStore->getValues();
                auto it = values->find(name.substr(prefixLen));
                if (it != values->end()) {
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
            } else if (name == "view::rotation") {
                return viewState.rotation;
            } else if (name == "view::tilt") {
                return viewState.tilt;
            } else if (name == "view::distance") {
                // Meters from the camera to the label being ranked. Only defined where the style
                // function is evaluated PER LABEL - the culler's ranking pass (see
                // TextLabelStyle::rankFunc); 0 in every other evaluation, including the one the
                // renderer does per batch.
                return viewState.labelDistance;
            }
            return Value();
        }
        return getVariable(name);
    }
}
