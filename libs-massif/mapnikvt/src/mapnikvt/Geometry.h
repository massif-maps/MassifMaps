/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_GEOMETRY_H_
#define _MASSIF_MAPNIKVT_GEOMETRY_H_

#include <memory>
#include <list>
#include <vector>
#include <variant>

#include <cglib/vec.h>

namespace massif::mvt {
    class PointGeometry final {
    public:
        using Vertices = std::vector<cglib::vec2<float>>;
        using VerticesList = std::vector<Vertices>;

        explicit PointGeometry(VerticesList verticesList) : _verticesList(std::move(verticesList)) { }

        const VerticesList& getVerticesList() const { return _verticesList; }
        const Vertices getVertices() const {
            Vertices flattened;
            for (auto const &v: _verticesList) {
                flattened.insert(flattened.end(), v.begin(), v.end());
            }
            return flattened;
        }

    private:
        VerticesList _verticesList;
    };

    class LineGeometry final {
    public:
        using Vertices = std::vector<cglib::vec2<float>>;
        using VerticesList = std::vector<Vertices>;

        explicit LineGeometry(VerticesList verticesList) : _verticesList(std::move(verticesList)) { }
        
        const VerticesList& getVerticesList() const { return _verticesList; }
        
        Vertices getMidPoints() const;

    private:
        VerticesList _verticesList;
    };

    class PolygonGeometry final {
    public:
        using Vertices = std::vector<cglib::vec2<float>>;
        using VerticesList = std::vector<Vertices>;
        using PolygonList = std::vector<VerticesList>;

        explicit PolygonGeometry(PolygonList polygonList) : _polygonList(std::move(polygonList)) { }
        
        const PolygonList& getPolygonList() const { return _polygonList; }

        VerticesList getClosedOuterRings(bool clip) const;

        Vertices getCenterPoints() const;
        Vertices getSurfacePoints() const;

    private:
        PolygonList _polygonList;
    };

    using Geometry = std::variant<PointGeometry, LineGeometry, PolygonGeometry>;
}

#endif
