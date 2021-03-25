#include "GeometrySymbolizer.h"

namespace carto { namespace mvt {
    void GeometrySymbolizer::bindParameter(const std::string& name, const std::string& value) {
        if (name == "geometry-transform") {
            bind(&_geometryTransform, parseStringExpression(value), &GeometrySymbolizer::convertTransform);
        }
        else {
            Symbolizer::bindParameter(name, value);
        }
    }

    std::optional<vt::Transform> GeometrySymbolizer::getGeometryTransform() const {
        return _geometryTransform != vt::Transform() ? _geometryTransform : std::optional<vt::Transform>();
    }
} }
