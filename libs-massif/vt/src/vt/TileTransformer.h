/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILETRANSFORMER_H_
#define _CARTO_VT_TILETRANSFORMER_H_

#include "TileId.h"
#include "VertexArray.h"

#include <cmath>
#include <memory>

#include <boost/math/constants/constants.hpp>

#include <cglib/vec.h>
#include <cglib/mat.h>
#include <cglib/bbox.h>

namespace carto { namespace vt {
    class TileTransformer {
    public:
        class VertexTransformer {
        public:
            virtual ~VertexTransformer() = default;

            virtual cglib::vec3<float> calculatePoint(const cglib::vec2<float>& pos) const = 0;
            virtual cglib::vec3<float> calculateNormal(const cglib::vec2<float>& pos) const = 0;
            virtual cglib::vec3<float> calculateVector(const cglib::vec2<float>& pos, const cglib::vec2<float>& vec) const = 0;
            virtual cglib::vec2<float> calculateTilePosition(const cglib::vec3<float>& pos) const = 0;
            virtual float calculateHeight(const cglib::vec2<float>& pos, float height) const = 0;

            virtual void tesselateLineString(const cglib::vec2<float>* points, std::size_t count, VertexArray<cglib::vec2<float>>& tesselatedPoints) const = 0;
            virtual void tesselateTriangles(const std::size_t* indices, std::size_t count, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<std::size_t>& tesselatedIndices) const = 0;
        };

        virtual ~TileTransformer() = default;

        virtual cglib::vec3<double> calculateTileOrigin(const TileId& tileId) const = 0;
        virtual cglib::bbox3<double> calculateTileBBox(const TileId& tileId) const = 0;
        virtual cglib::mat4x4<double> calculateTileMatrix(const TileId& tileId, float coordScale) const = 0;
        virtual cglib::mat4x4<float> calculateTileTransform(const TileId& tileId, const cglib::vec2<float>& translate, float coordScale) const = 0;

        virtual std::shared_ptr<const VertexTransformer> createTileVertexTransformer(const TileId& tileId) const = 0;

    protected:
        inline static constexpr double PI = boost::math::constants::pi<double>();
        inline static constexpr double EARTH_RADIUS = 6378137.0;
        inline static constexpr double EARTH_CIRCUMFERENCE = 2.0 * PI * EARTH_RADIUS;
    };

    class DefaultTileTransformer final : public TileTransformer {
    public:
        class DefaultVertexTransformer final : public VertexTransformer {
        public:
            explicit DefaultVertexTransformer(const TileId& tileId);
            virtual ~DefaultVertexTransformer() = default;

            virtual cglib::vec3<float> calculatePoint(const cglib::vec2<float>& pos) const override;
            virtual cglib::vec3<float> calculateNormal(const cglib::vec2<float>& pos) const override;
            virtual cglib::vec3<float> calculateVector(const cglib::vec2<float>& pos, const cglib::vec2<float>& vec) const override;
            virtual cglib::vec2<float> calculateTilePosition(const cglib::vec3<float>& pos) const override;
            virtual float calculateHeight(const cglib::vec2<float>& pos, float height) const override;

            virtual void tesselateLineString(const cglib::vec2<float>* points, std::size_t count, VertexArray<cglib::vec2<float>>& tesselatedPoints) const override;
            virtual void tesselateTriangles(const std::size_t* indices, std::size_t count, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<std::size_t>& tesselatedIndices) const override;

        private:
            const TileId _tileId;
        };
        
        explicit DefaultTileTransformer(float scale) : _scale(scale) { }
        virtual ~DefaultTileTransformer() = default;

        virtual cglib::vec3<double> calculateTileOrigin(const TileId& tileId) const override;
        virtual cglib::bbox3<double> calculateTileBBox(const TileId& tileId) const override;
        virtual cglib::mat4x4<double> calculateTileMatrix(const TileId& tileId, float coordScale) const override;
        virtual cglib::mat4x4<float> calculateTileTransform(const TileId& tileId, const cglib::vec2<float>& translate, float coordScale) const override;

        virtual std::shared_ptr<const VertexTransformer> createTileVertexTransformer(const TileId& tileId) const override;
    
    private:
        const float _scale;
    };

    class SphericalTileTransformer final : public TileTransformer {
    public:
        class SphericalVertexTransformer final : public VertexTransformer {
        public:
            explicit SphericalVertexTransformer(const TileId& tileId, const cglib::vec3<double>& origin, float divideThreshold);
            virtual ~SphericalVertexTransformer() = default;

            virtual cglib::vec3<float> calculatePoint(const cglib::vec2<float>& pos) const override;
            virtual cglib::vec3<float> calculateNormal(const cglib::vec2<float>& pos) const override;
            virtual cglib::vec3<float> calculateVector(const cglib::vec2<float>& pos, const cglib::vec2<float>& vec) const override;
            virtual cglib::vec2<float> calculateTilePosition(const cglib::vec3<float>& pos) const override;
            virtual float calculateHeight(const cglib::vec2<float>& pos, float height) const override;

            virtual void tesselateLineString(const cglib::vec2<float>* points, std::size_t count, VertexArray<cglib::vec2<float>>& tesselatedPoints) const override;
            virtual void tesselateTriangles(const std::size_t* indices, std::size_t count, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<std::size_t>& tesselatedIndices) const override;

        private:
            cglib::vec2<double> tileToEPSG3857(const cglib::vec2<float>& pos) const;
            cglib::vec2<float> epsg3857ToTile(const cglib::vec2<double>& epsg3857Pos) const;
            cglib::vec3<double> tileToSpherical(const cglib::vec2<float>& pos) const;
            cglib::vec2<float> sphericalToTile(const cglib::vec3<double>& p) const;

            void tesselateSegment(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, float dist, VertexArray<cglib::vec2<float>>& points) const;
            void tesselateTriangle(std::size_t i0, std::size_t i1, std::size_t i2, float dist01, float dist02, float dist12, VertexArray<cglib::vec2<float>>& coords, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<std::size_t>& indices) const;

            const TileId _tileId;
            const cglib::vec3<double> _origin;
            const float _divideThreshold;
            const cglib::vec2<double> _tileOffset;
            const double _tileScale;
        };

        explicit SphericalTileTransformer(float scale) : _scale(scale) { }
        virtual ~SphericalTileTransformer() = default;

        float getDivideThreshold() const { return _divideThreshold; }
        void setDivideThreshold(float divideThreshold) { _divideThreshold = divideThreshold; }

        virtual cglib::vec3<double> calculateTileOrigin(const TileId& tileId) const override;
        virtual cglib::bbox3<double> calculateTileBBox(const TileId& tileId) const override;
        virtual cglib::mat4x4<double> calculateTileMatrix(const TileId& tileId, float coordScale) const override;
        virtual cglib::mat4x4<float> calculateTileTransform(const TileId& tileId, const cglib::vec2<float>& translate, float coordScale) const override;

        virtual std::shared_ptr<const VertexTransformer> createTileVertexTransformer(const TileId& tileId) const override;

    private:
        static cglib::vec2<double> tileOffset(const TileId& tileId);
        static double tileScale(const TileId& tileId);
        static cglib::vec3<double> epsg3857ToSpherical(const cglib::vec2<double>& epsg3857Pos);
        static cglib::vec2<double> sphericalToEPSG3857(const cglib::vec3<double>& p);

        inline static constexpr int ZOOM_0_GRID_SIZE = 64;
        inline static constexpr float DEFAULT_DIVIDE_THRESHOLD = static_cast<float>(EARTH_CIRCUMFERENCE / ZOOM_0_GRID_SIZE);

        const double _scale;

        float _divideThreshold = DEFAULT_DIVIDE_THRESHOLD;
    };
} }

#endif
