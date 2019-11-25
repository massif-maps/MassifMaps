#include "TileGeometryIterator.h"

namespace carto { namespace vt {
    TileGeometryIterator::TileGeometryIterator(const TileId& tileId, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<const TileGeometry>& geometry, const std::shared_ptr<const TileTransformer>& transformer, const ViewState& viewState, float buffer, float scale, float heightScale) :
        _viewState(viewState), _buffer(buffer), _scale(scale), _heightScale(heightScale), _geometry(geometry), _transformMatrix(cglib::mat4x4<float>::identity())
    {
        const TileGeometry::StyleParameters& styleParams = _geometry->getStyleParameters();
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        if (styleParams.translate) {
            float zoomScale = std::pow(2.0f, tileId.zoom - _viewState.zoom);
            cglib::vec2<float> translate = (*styleParams.translate) * zoomScale;
            _transformMatrix = transformer->calculateTileTransform(tileId, translate, 1.0f / vertexGeomLayoutParams.coordScale);
        }
    }

    long long TileGeometryIterator::getId(std::size_t index) const {
        for (std::size_t i = 0; i < _geometry->getIds().size(); i++) {
            if (index < _geometry->getIds()[i].first) {
                return _geometry->getIds()[i].second;
            }
            index -= _geometry->getIds()[i].first;
        }
        return 0;
    }
    
    TileGeometryIterator::Triangle TileGeometryIterator::getTriangle(std::size_t index) const {
        std::size_t i0 = _geometry->getIndices()[index + 0];
        std::size_t i1 = _geometry->getIndices()[index + 1];
        std::size_t i2 = _geometry->getIndices()[index + 2];

        cglib::vec3<float> p0 = decodeVertex(i0);
        cglib::vec3<float> p1 = decodeVertex(i1);
        cglib::vec3<float> p2 = decodeVertex(i2);

        if (_geometry->getType() == TileGeometry::Type::POINT) {
            p0 += decodePointOffset(i0);
            p1 += decodePointOffset(i1);
            p2 += decodePointOffset(i2);
        }
        else if (_geometry->getType() == TileGeometry::Type::LINE) {
            p0 += decodeLineOffset(i0);
            p1 += decodeLineOffset(i1);
            p2 += decodeLineOffset(i2);
        }
        else if (_geometry->getType() == TileGeometry::Type::POLYGON) {
            // do not extend
        }
        else if (_geometry->getType() == TileGeometry::Type::POLYGON3D) {
            p0 += decodePolygon3DOffset(i0);
            p1 += decodePolygon3DOffset(i1);
            p2 += decodePolygon3DOffset(i2);
        }

        return std::array<cglib::vec3<float>, 3> {{ p0, p1, p2 }};
    }

    cglib::vec3<float> TileGeometryIterator::decodeVertex(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        std::size_t coordOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.coordOffset;
        const std::int16_t* coordPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[coordOffset]);
        cglib::vec3<float> pos(0, 0, 0);
        for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
            pos(i) = coordPtr[i];
        }
        return cglib::transform_point(pos, _transformMatrix) * (1.0f / vertexGeomLayoutParams.coordScale);
    }

    cglib::vec3<float> TileGeometryIterator::decodePointOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        std::size_t binormalOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.binormalOffset;
        const std::int16_t* binormalPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[binormalOffset]);
        int styleIndex = 0;
        if (vertexGeomLayoutParams.attribsOffset >= 0) {
            std::size_t attribOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.attribsOffset;
            const std::int8_t* attribPtr = reinterpret_cast<const std::int8_t*>(&_geometry->getVertexGeometry()[attribOffset]);
            styleIndex = attribPtr[0];
        }
        float size = std::abs((_geometry->getStyleParameters().widthFuncs[styleIndex])(_viewState));
        cglib::vec3<float> binormal(0, 0, 0);
        for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
            binormal(i) = binormalPtr[i] * (size / vertexGeomLayoutParams.binormalScale);
        }
        if (_buffer != 0 && cglib::norm(binormal) > 0) {
            binormal = binormal * (1.0f + _buffer / cglib::length(binormal) * std::sqrt(2.0f)); // enlarge buffer artifically as binormal is usually a diagonal offset
        }
        return binormal * _scale;
    }

    cglib::vec3<float> TileGeometryIterator::decodeLineOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        std::size_t binormalOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.binormalOffset;
        const std::int16_t* binormalPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[binormalOffset]);
        int styleIndex = 0;
        if (vertexGeomLayoutParams.attribsOffset >= 0) {
            std::size_t attribOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.attribsOffset;
            const std::int8_t* attribPtr = reinterpret_cast<const std::int8_t*>(&_geometry->getVertexGeometry()[attribOffset]);
            styleIndex = attribPtr[0];
        }
        float width = 0.5f * std::abs((_geometry->getStyleParameters().widthFuncs[styleIndex])(_viewState));
        cglib::vec3<float> binormal(0, 0, 0);
        for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
            binormal(i) = binormalPtr[i] * (width / vertexGeomLayoutParams.binormalScale);
        }
        if (_buffer != 0 && cglib::norm(binormal) > 0) {
            binormal = binormal * (1.0f + _buffer / cglib::length(binormal) / 2.0f); // artifically decrease buffering
        }
        return binormal * _scale;
    }

    cglib::vec3<float> TileGeometryIterator::decodePolygon3DOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        std::size_t heightOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.heightOffset;
        const std::int16_t* heightPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[heightOffset]);
        return cglib::vec3<float>(0, 0, *heightPtr * _heightScale) * (1.0f / vertexGeomLayoutParams.heightScale);
    }
} }
