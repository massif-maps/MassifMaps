/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINTILETRANSFORMER_H_
#define _MASSIF_TERRAINTILETRANSFORMER_H_

#include <memory>

#include <vt/TileTransformer.h>

namespace massif {
    class ElevationManager;
    class ElevationTileGrid;

    /**
     * Adds terrain to a base transformer (plane or globe): geometry is built flat and displaced by the shader,
     * so this only adds subdivision and an elevation-grown bbox. Thresholds are in world units, split by
     * binary halving, so different-zoom tiles subdivide at the same points. Internal, not in the public API.
     */
    class TerrainTileTransformer final : public vt::TileTransformer {
    public:
        class TerrainVertexTransformer final : public VertexTransformer {
        public:
            TerrainVertexTransformer(const vt::TileId& tileId, std::shared_ptr<const VertexTransformer> base, std::shared_ptr<ElevationTileGrid> grid, float exaggeration, float divideThreshold, float lineDivideThreshold, float latticeCell, float sagToleranceMeters);
            virtual ~TerrainVertexTransformer() = default;

            virtual cglib::vec3<float> calculatePoint(const cglib::vec2<float>& pos) const override;
            virtual cglib::vec3<float> calculateNormal(const cglib::vec2<float>& pos) const override;
            virtual cglib::vec3<float> calculateVector(const cglib::vec2<float>& pos, const cglib::vec2<float>& vec) const override;
            virtual cglib::vec2<float> calculateTilePosition(const cglib::vec3<float>& pos) const override;
            virtual float calculateHeight(const cglib::vec2<float>& pos, float height) const override;

            virtual void tesselateLineString(const cglib::vec2<float>* points, std::size_t count, vt::VertexArray<cglib::vec2<float>>& tesselatedPoints) const override;
            virtual void tesselateLabelLineString(const cglib::vec2<float>* points, std::size_t count, vt::VertexArray<cglib::vec2<float>>& tesselatedPoints) const override;
            virtual void tesselateTriangles(const std::size_t* indices, std::size_t count, vt::VertexArray<cglib::vec2<float>>& coords, vt::VertexArray<cglib::vec2<float>>& texCoords, vt::VertexArray<std::size_t>& tesselatedIndices) const override;

        private:
            // The terrain half of each tesselation; the public entry points then hand the result to the base.
            void tesselateLineStringTerrain(const cglib::vec2<float>* points, std::size_t count, vt::VertexArray<cglib::vec2<float>>& tesselatedPoints) const;
            void tesselateLabelLineStringTerrain(const cglib::vec2<float>* points, std::size_t count, vt::VertexArray<cglib::vec2<float>>& tesselatedPoints) const;
            void tesselateTrianglesTerrain(const std::size_t* indices, std::size_t count, vt::VertexArray<cglib::vec2<float>>& coords, vt::VertexArray<cglib::vec2<float>>& texCoords, vt::VertexArray<std::size_t>& tesselatedIndices) const;

            double calculateLocalHeight(const cglib::vec2<float>& pos) const;

            void tesselateSegment(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, float dist, float threshold, vt::VertexArray<cglib::vec2<float>>& points) const;
            // Splits only where the terrain leaves the chord, until the sag is under tolerance; unlike the
            // lattice, a flat valley floor is not cut at all.
            void tesselateSegmentBySag(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, double h0, double h1, float dist, int depth, vt::VertexArray<cglib::vec2<float>>& points) const;
            // Splits at surface cell edges and diagonals so each sub-segment lies in one surface triangle.
            // False when it spans too many cells; the caller then halves by threshold.
            bool tesselateSegmentOnLattice(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, vt::VertexArray<cglib::vec2<float>>& points) const;
            void tesselateTriangle(std::size_t i0, std::size_t i1, std::size_t i2, float dist01, float dist02, float dist12, vt::VertexArray<cglib::vec2<float>>& coords, vt::VertexArray<cglib::vec2<float>>& texCoords, vt::VertexArray<std::size_t>& indices) const;

            const vt::TileId _tileId;
            const std::shared_ptr<const VertexTransformer> _base;
            const std::shared_ptr<ElevationTileGrid> _grid;
            const float _exaggeration;
            const float _divideThreshold; // triangle subdivision, EPSG3857 meters; infinity disables subdivision
            const float _lineDivideThreshold; // line subdivision, EPSG3857 meters; finer than the triangle threshold in regular-grid mode
            const float _latticeCell; // surface grid cell size in tile-local units; 0 outside regular-grid mode
            float _sagToleranceLocal = 0.0f; // max chord sag in tile-local height units; 0 disables sag subdivision
            float _sagMinSegmentMeters = 0.0f; // never cut below the elevation data's own resolution
            double _tileScaleMeters; // tile-local length to metres, at the equator
        };

        TerrainTileTransformer(std::shared_ptr<const vt::TileTransformer> base, const std::shared_ptr<ElevationManager>& elevationManager, int meshResolution, int minZoom, bool sourceDensity, bool sourceDensityLines, bool flatContentDraped);
        virtual ~TerrainTileTransformer() = default;

        const std::shared_ptr<const vt::TileTransformer>& getBase() const { return _base; }
        std::shared_ptr<ElevationManager> getElevationManager() const { return _elevationManager; }
        int getMeshResolution() const { return _meshResolution; }
        int getMinZoom() const { return _minZoom; }

        virtual bool isElevationBased() const override { return true; }
        virtual bool isFlatContentDraped() const override { return _flatContentDraped; }
        virtual bool isSpherical() const override { return _base->isSpherical(); }

        virtual cglib::vec3<double> calculateTileOrigin(const vt::TileId& tileId) const override;
        virtual cglib::bbox3<double> calculateTileBBox(const vt::TileId& tileId) const override;
        virtual cglib::mat4x4<double> calculateTileMatrix(const vt::TileId& tileId, float coordScale) const override;
        virtual cglib::mat4x4<float> calculateTileTransform(const vt::TileId& tileId, const cglib::vec2<float>& translate, float coordScale) const override;

        virtual std::shared_ptr<const VertexTransformer> createTileVertexTransformer(const vt::TileId& tileId) const override;
        // Both are the base's: terrain adds height to the tile geometry, not to the projection.
        virtual cglib::vec3<double> calculateMercatorPos(const cglib::vec3<double>& pos) const override { return _base->calculateMercatorPos(pos); }
        virtual cglib::vec3<double> calculateElevatedPos(const cglib::vec3<double>& pos, double height) const override { return _base->calculateElevatedPos(pos, height); }

    private:
        // At the tile's centre latitude: the Mercator stretch the elevation range carries and the base's world does not.
        static double metersPerInternalUnit(const vt::TileId& tileId);

        static constexpr float FLAT_HEIGHT_RANGE_EPSILON = 0.001f;
        // Draped lines subdivide to this fraction of a surface cell so they stop chording its diagonal fold.
        // Triangles stay at one cell: their sag is bounded and area content costs 1/factor^2.
        static constexpr double REGULAR_GRID_LINE_SUBDIVISION = 0.35;
        // Past this, halving is cheaper than the lattice crossing list.
        static constexpr int MAX_LATTICE_SPLITS_PER_SEGMENT = 64;

        // Recursion guard for the sag split: bounds the work a pathological cliff can demand.
        static constexpr int MAX_SAG_SPLIT_DEPTH = 10;

        const std::shared_ptr<const vt::TileTransformer> _base;
        const std::shared_ptr<ElevationManager> _elevationManager;
        const int _meshResolution;
        const int _minZoom; // tiles below this zoom level are rendered flat
        const bool _sourceDensity; // source-density (tangram) mode: do not subdivide draped fills; GPU-displace at source density + lifting depth slack
        const bool _sourceDensityLines; // also skip line subdivision (draped lines are baked flat)
        const bool _flatContentDraped;
    };
}

#endif
