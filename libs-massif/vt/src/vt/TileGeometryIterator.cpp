#include "TileGeometryIterator.h"

namespace carto { namespace vt {
    TileGeometryIterator::TileGeometryIterator(const TileId& tileId, const std::shared_ptr<const TileGeometry>& geometry, const std::shared_ptr<const TileTransformer>& transformer, const ViewState& viewState, float pointBuffer, float lineBuffer, float scale, float heightScale) :
        _viewState(viewState), _pointBuffer(pointBuffer), _lineBuffer(lineBuffer), _scale(scale), _heightScale(heightScale), _geometry(geometry), _transformMatrix(cglib::mat4x4<float>::identity())
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

    cglib::vec4<std::int8_t> TileGeometryIterator::getAttribs(std::size_t index) const {
        std::size_t i0 = _geometry->getIndices()[index + 0];

        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        cglib::vec4<std::int8_t> attribs(0, 0, 0, 0);
        if (vertexGeomLayoutParams.attribsOffset >= 0) {
            std::size_t attribOffset = i0 * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.attribsOffset;
            const std::int8_t* attribPtr = reinterpret_cast<const std::int8_t*>(&_geometry->getVertexGeometry()[attribOffset]);
            attribs = cglib::vec4<std::int8_t>(attribPtr[0], attribPtr[1], attribPtr[2], attribPtr[3]);
        }
        return attribs;
    }
    
    TileGeometryIterator::TriangleCoords TileGeometryIterator::getTriangleCoords(std::size_t index) const {
        std::size_t i0 = _geometry->getIndices()[index + 0];
        std::size_t i1 = _geometry->getIndices()[index + 1];
        std::size_t i2 = _geometry->getIndices()[index + 2];

        cglib::vec3<float> p0 = decodeVertexPos(i0);
        cglib::vec3<float> p1 = decodeVertexPos(i1);
        cglib::vec3<float> p2 = decodeVertexPos(i2);

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

    TileGeometryIterator::TriangleUVs TileGeometryIterator::getTriangleUVs(std::size_t index) const {
        std::size_t i0 = _geometry->getIndices()[index + 0];
        std::size_t i1 = _geometry->getIndices()[index + 1];
        std::size_t i2 = _geometry->getIndices()[index + 2];

        cglib::vec2<float> uv0 = decodeVertexUV(i0);
        cglib::vec2<float> uv1 = decodeVertexUV(i1);
        cglib::vec2<float> uv2 = decodeVertexUV(i2);

        return std::array<cglib::vec2<float>, 3> {{ uv0, uv1, uv2 }};
    }

    cglib::vec3<float> TileGeometryIterator::decodeVertexPos(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        cglib::vec3<float> pos(0, 0, 0);
        if (vertexGeomLayoutParams.coordOffset >= 0) {
            std::size_t coordOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.coordOffset;
            const std::int16_t* coordPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[coordOffset]);
            for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
                pos(i) = coordPtr[i];
            }
        }
        return cglib::transform_point(pos, _transformMatrix) * (1.0f / vertexGeomLayoutParams.coordScale);
    }

    cglib::vec2<float> TileGeometryIterator::decodeVertexUV(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        cglib::vec2<float> uv(0, 0);
        if (vertexGeomLayoutParams.texCoordOffset >= 0) {
            std::size_t texCoordOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.texCoordOffset;
            const std::int16_t* texCoordPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[texCoordOffset]);
            for (int i = 0; i < 2; i++) {
                uv(i) = texCoordPtr[i] * (1.0f / vertexGeomLayoutParams.texCoordScale);
            }
        }
        return uv;
    }

    cglib::vec3<float> TileGeometryIterator::decodePointOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        int styleIndex = 0;
        if (vertexGeomLayoutParams.attribsOffset >= 0) {
            std::size_t attribOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.attribsOffset;
            const std::int8_t* attribPtr = reinterpret_cast<const std::int8_t*>(&_geometry->getVertexGeometry()[attribOffset]);
            styleIndex = attribPtr[0];
        }
        cglib::vec3<float> binormal(0, 0, 0);
        if (vertexGeomLayoutParams.binormalOffset >= 0) {
            std::size_t binormalOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.binormalOffset;
            const std::int16_t* binormalPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[binormalOffset]);
            float size = std::abs((_geometry->getStyleParameters().widthFuncs[styleIndex])(_viewState));
            for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
                binormal(i) = binormalPtr[i] * (size / vertexGeomLayoutParams.binormalScale);
            }
            if (_pointBuffer != 0 && cglib::norm(binormal) > 0) {
                binormal = binormal * (1.0f + _pointBuffer / cglib::length(binormal));
            }
        }
        return binormal * _scale;
    }

    cglib::vec3<float> TileGeometryIterator::decodeLineOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        int styleIndex = 0;
        int side = 0;
        if (vertexGeomLayoutParams.attribsOffset >= 0) {
            std::size_t attribOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.attribsOffset;
            const std::int8_t* attribPtr = reinterpret_cast<const std::int8_t*>(&_geometry->getVertexGeometry()[attribOffset]);
            styleIndex = attribPtr[0];
            side = attribPtr[2];
        }
        cglib::vec3<float> binormal(0, 0, 0);
        if (vertexGeomLayoutParams.binormalOffset >= 0) {
            std::size_t binormalOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.binormalOffset;
            const std::int16_t* binormalPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[binormalOffset]);
            float width = 0.5f * std::abs((_geometry->getStyleParameters().widthFuncs[styleIndex])(_viewState));
            for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
                binormal(i) = binormalPtr[i] * (width / vertexGeomLayoutParams.binormalScale);
            }
            if (_lineBuffer != 0 && cglib::norm(binormal) > 0) {
                binormal = binormal * (1.0f + _lineBuffer / cglib::length(binormal));
            }
            float offset = (_geometry->getStyleParameters().offsetFuncs[styleIndex])(_viewState);
            if (offset != 0) {
                for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
                    binormal(i) -= binormalPtr[i] * (offset * side / vertexGeomLayoutParams.binormalScale);
                }
            }
        }
        return binormal * _scale;
    }

    cglib::vec3<float> TileGeometryIterator::decodePolygon3DOffset(std::size_t index) const {
        const TileGeometry::VertexGeometryLayoutParameters& vertexGeomLayoutParams = _geometry->getVertexGeometryLayoutParameters();
        cglib::vec3<float> normal(0, 0, 1);
        if (vertexGeomLayoutParams.normalOffset >= 0) {
            std::size_t normalOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.normalOffset;
            const std::int16_t* normalPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[normalOffset]);
            normal = cglib::vec3<float>(0, 0, 0);
            for (int i = 0; i < vertexGeomLayoutParams.dimensions; i++) {
                normal(i) = normalPtr[i] * (1.0f / 32767.0f);
            }
        }
        float height = 0;
        if (vertexGeomLayoutParams.heightOffset >= 0) {
            std::size_t heightOffset = index * vertexGeomLayoutParams.vertexSize + vertexGeomLayoutParams.heightOffset;
            const std::int16_t* heightPtr = reinterpret_cast<const std::int16_t*>(&_geometry->getVertexGeometry()[heightOffset]);
            height = *heightPtr * (1.0f / vertexGeomLayoutParams.heightScale);
        }
        return normal * (height * _heightScale);
    }
} }
