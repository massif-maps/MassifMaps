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
        // partIndices: each part's index in the source feature, when an overzoom clip dropped some.
        // Label ids are built from it, so one point keeps one id in every tile cut from its source.
        PointGeometry(VerticesList verticesList, std::vector<int> partIndices) : _verticesList(std::move(verticesList)), _partIndices(std::move(partIndices)) { }

        const VerticesList& getVerticesList() const { return _verticesList; }
        int getPartIndex(std::size_t part) const { return _partIndices.empty() ? static_cast<int>(part) : _partIndices[part]; }
        // Of the source feature: part indices exist only when a clip dropped some of several parts.
        bool isMultiPoint() const { return !_partIndices.empty() || _verticesList.size() > 1; }
        const Vertices getVertices() const {
            Vertices flattened;
            for (auto const &v: _verticesList) {
                flattened.insert(flattened.end(), v.begin(), v.end());
            }
            return flattened;
        }

    private:
        VerticesList _verticesList;
        std::vector<int> _partIndices;
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
