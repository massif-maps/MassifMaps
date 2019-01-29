#include "TileGeometryIterator.h"

namespace carto { namespace vt {
    TileGeometryIterator::TileGeometryIterator(const TileId& tileId, const std::shared_ptr<const Tile>& tile, const std::shared_ptr<const TileGeometry>& geometry, const std::shared_ptr<const TileTransformer>& transformer, const ViewState& viewState, float delta, float scale, float heightScale) :
        _viewState(viewState), _delta(delta), _scale(scale), _heightScale(heightScale), _geometry(geometry), _transformMatrix(cglib::mat4x4<float>::identity())
    {
        const TileGeometry::StyleParameters& styleParams = _geometry->getStyleParameters();
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        if (styleParams.translate) {
            float zoomScale = std::exp2(tileId.zoom - _viewState.zoom);
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
        const short* coordPtr = reinterpret_cast<const short*>(&_geometry->getVertexGeometry()[coordOffset]);
        cglib::vec3<float> pos(0, 0, 0);
        for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
            pos(i) = coordPtr[i];
        }
        return cglib::transform_point(pos, _transformMatrix) * (1.0f / vertexGeomLayoutParams.coordScale);
    }

    cglib::vec3<float> TileGeometryIterator::decodePointOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        std::size_t binormalOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.binormalOffset;
        const short* binormalPtr = reinterpret_cast<const short*>(&_geometry->getVertexGeometry()[binormalOffset]);
        std::size_t attribOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.attribsOffset;
        const char* attribPtr = reinterpret_cast<const char*>(&_geometry->getVertexGeometry()[attribOffset]);
        float size = std::abs((_geometry->getStyleParameters().widthFuncs[attribPtr[0]])(_viewState));
        if (size > 0) {
            size += _delta;
        }
        cglib::vec3<float> binormal(0, 0, 0);
        for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
            binormal(i) = binormalPtr[i] * (size * _scale);
        }
        return binormal * (1.0f / vertexGeomLayoutParams.binormalScale);
    }

    cglib::vec3<float> TileGeometryIterator::decodeLineOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        std::size_t binormalOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.binormalOffset;
        const short* binormalPtr = reinterpret_cast<const short*>(&_geometry->getVertexGeometry()[binormalOffset]);
        std::size_t attribOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.attribsOffset;
        const char* attribPtr = reinterpret_cast<const char*>(&_geometry->getVertexGeometry()[attribOffset]);
        float width = 0.5f * std::abs((_geometry->getStyleParameters().widthFuncs[attribPtr[0]])(_viewState));
        if (width > 0) {
            width += _delta;
        }
        cglib::vec3<float> binormal(0, 0, 0);
        for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
            binormal(i) = binormalPtr[i] * (width * _scale);
        }
        return binormal * (1.0f / vertexGeomLayoutParams.binormalScale);
    }

    cglib::vec3<float> TileGeometryIterator::decodePolygon3DOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        std::size_t heightOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.heightOffset;
        const short* heightPtr = reinterpret_cast<const short*>(&_geometry->getVertexGeometry()[heightOffset]);
        return cglib::vec3<float>(0, 0, *heightPtr * _heightScale) * (1.0f / vertexGeomLayoutParams.heightScale);
    }
} }
