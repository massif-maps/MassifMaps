/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILEGEOMETRY_H_
#define _CARTO_VT_TILEGEOMETRY_H_

#include "Bitmap.h"
#include "Color.h"
#include "StrokeMap.h"
#include "VertexArray.h"
#include "Styles.h"

#include <memory>
#include <optional>
#include <array>
#include <vector>

#include <cglib/mat.h>

namespace carto { namespace vt {
    class TileGeometry final {
    public:
        enum class Type {
            NONE, POINT, LINE, POLYGON, POLYGON3D
        };

        struct StyleParameters {
            inline static constexpr int MAX_PARAMETERS = 16;

            int parameterCount;
            std::array<ColorFunction, MAX_PARAMETERS> colorFuncs;
            std::array<FloatFunction, MAX_PARAMETERS> widthFuncs;
            std::array<FloatFunction, MAX_PARAMETERS> strokeWidthFuncs;
            std::array<float, MAX_PARAMETERS> strokeScales;
            std::shared_ptr<const BitmapPattern> pattern;
            std::optional<cglib::vec2<float>> translate;
            CompOp compOp;

            StyleParameters() : parameterCount(0), colorFuncs(), widthFuncs(), strokeWidthFuncs(), strokeScales(), pattern(), translate(), compOp(CompOp::SRC_OVER) { }
        };

        struct VertexGeometryLayoutParameters {
            int vertexSize;
            int dimensions;
            int coordOffset;
            int attribsOffset;
            int texCoordOffset;
            int normalOffset;
            int binormalOffset;
            int heightOffset;
            float coordScale;
            float texCoordScale;
            float binormalScale;
            float heightScale;

            VertexGeometryLayoutParameters() : vertexSize(0), dimensions(2), coordOffset(-1), attribsOffset(-1), texCoordOffset(-1), normalOffset(-1), binormalOffset(-1), heightOffset(-1), coordScale(0), texCoordScale(0), binormalScale(0), heightScale(0) { }
        };

        explicit TileGeometry(Type type, float geomScale, const StyleParameters& styleParameters, const VertexGeometryLayoutParameters& vertexGeometryLayoutParameters, VertexArray<std::uint8_t> vertexGeometry, VertexArray<std::uint16_t> indices, std::vector<std::pair<std::size_t, long long>> ids) : _type(type), _geomScale(geomScale), _styleParameters(styleParameters), _vertexGeometryLayoutParameters(vertexGeometryLayoutParameters), _indicesCount(static_cast<unsigned int>(indices.size())), _vertexGeometry(std::move(vertexGeometry)), _indices(std::move(indices)), _ids(std::move(ids)) { }

        Type getType() const { return _type; }
        float getGeometryScale() const { return _geomScale; }
        const StyleParameters& getStyleParameters() const { return _styleParameters; }
        const VertexGeometryLayoutParameters& getVertexGeometryLayoutParameters() const { return _vertexGeometryLayoutParameters; }
        unsigned int getIndicesCount() const { return _indicesCount; }

        const VertexArray<std::uint8_t>& getVertexGeometry() const { return _vertexGeometry; }
        const VertexArray<std::uint16_t>& getIndices() const { return _indices; }
        const std::vector<std::pair<std::size_t, long long>>& getIds() const { return _ids; }

        void releaseVertexArrays() {
            _vertexGeometry.clear();
            _vertexGeometry.shrink_to_fit();
            _indices.clear();
            _indices.shrink_to_fit();
            _ids.clear();
            _ids.shrink_to_fit();
        }

        std::size_t getFeatureCount() const {
            switch (_type) {
            case Type::POINT:
            case Type::LINE:
                return _indicesCount / 6;
            case Type::POLYGON:
            case Type::POLYGON3D:
                return _indicesCount / 3;
            default:
                return 0;
            }
        }

        std::size_t getResidentSize() const {
            return 16 + _vertexGeometry.size() * sizeof(std::uint8_t) + _indices.size() * sizeof(std::uint16_t) + _ids.size() * sizeof(std::pair<std::size_t, long long>);
        }

    private:
        const Type _type;
        const float _geomScale;
        const StyleParameters _styleParameters;
        const VertexGeometryLayoutParameters _vertexGeometryLayoutParameters;
        const unsigned int _indicesCount; // real count, even if indices are released

        VertexArray<std::uint8_t> _vertexGeometry;
        VertexArray<std::uint16_t> _indices;
        std::vector<std::pair<std::size_t, long long>> _ids; // vertex count, feature id
    };
} }

#endif
