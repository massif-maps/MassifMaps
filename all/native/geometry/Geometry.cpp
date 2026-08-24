#include "Geometry.h"
#include "geometry/GeoJSONGeometryWriter.h"

#include <memory>

namespace massif {

    std::string Geometry::getGeoJSON() const {
        GeoJSONGeometryWriter writer;
        // An aliasing shared_ptr with no owner: the writer only walks the geometry, and the caller
        // already holds it alive.
        std::shared_ptr<Geometry> self(std::shared_ptr<Geometry>(), const_cast<Geometry*>(this));
        return writer.writeGeometry(self);
    }

}
