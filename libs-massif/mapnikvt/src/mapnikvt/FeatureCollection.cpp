#include "FeatureCollection.h"

#include <algorithm>

namespace {
    template <typename T>
    bool isRingCCW(const std::vector<cglib::vec2<T>>& vertices) {
        T area = 0;
        if (!vertices.empty()) {
            for (std::size_t i = 1; i < vertices.size(); i++) {
                area += vertices[i - 1](0) * vertices[i](1) - vertices[i](0) * vertices[i - 1](1);
            }
            area += vertices.back()(0) * vertices.front()(1) - vertices.front()(0) * vertices.back()(1);
        }
        return area > 0;
    }
}

namespace carto { namespace mvt {
    void FeatureCollection::clear() {
        _features.clear();
    }
        
    void FeatureCollection::append(long long localId, const Feature& feature) {
        _features.emplace_back(localId, feature);
    }

    std::size_t FeatureCollection::size() const {
        return _features.size();
    }

    std::shared_ptr<const LineGeometry> FeatureCollection::getLineGeometry(std::size_t index) const {
        const std::shared_ptr<const Geometry>& geometry = _features.at(index).second.getGeometry();
        if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(geometry)) {
            return lineGeometry;
        }
        if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(geometry)) {
            LineGeometry::VerticesList verticesList;
            for (const PolygonGeometry::VerticesList& polygon : polygonGeometry->getPolygonList()) {
                for (PolygonGeometry::Vertices vertices : polygon) {
                    if (vertices.empty()) {
                        continue;
                    }
                    cglib::vec2<float> first = vertices.front();
                    if (first != vertices.back()) {
                        vertices.push_back(first);
                    }
                    verticesList.push_back(std::move(vertices));
                }
            }
            return std::make_shared<LineGeometry>(std::move(verticesList));
        }
        return std::shared_ptr<const LineGeometry>();
    }
    
    std::shared_ptr<const PolygonGeometry> FeatureCollection::getPolygonGeometry(std::size_t index) const {
        const std::shared_ptr<const Geometry>& geometry = _features.at(index).second.getGeometry();
        if (auto polygonGeometry = std::dynamic_pointer_cast<const PolygonGeometry>(geometry)) {
            return polygonGeometry;
        }
        if (auto lineGeometry = std::dynamic_pointer_cast<const LineGeometry>(geometry)) {
            PolygonGeometry::PolygonList polygonList;
            for (LineGeometry::Vertices vertices : lineGeometry->getVerticesList()) {
                if (vertices.empty() || vertices.front() != vertices.back()) {
                    continue;
                }
                if (!isRingCCW(vertices)) {
                    std::reverse(vertices.begin(), vertices.end());
                }
                polygonList.push_back(PolygonGeometry::VerticesList { std::move(vertices) });
            }
            return std::make_shared<PolygonGeometry>(std::move(polygonList));
        }
        return std::shared_ptr<const PolygonGeometry>();
    }
} }
