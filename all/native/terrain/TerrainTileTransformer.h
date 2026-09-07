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
     * A planar tile transformer that displaces geometry by terrain elevation.
     * Heights are sampled CPU-side from the decoded elevation grids of the ElevationManager
     * at tile build time. Heights are a pure function of world position (the same DEM data
     * and deterministic parent-fallback rules are used by all tiles), so neighboring tiles at
     * different zoom levels produce matching edge geometry.
     * Line strings and triangles are adaptively subdivided with world-unit thresholds
     * (binary halving) so that subdivision points of different-zoom tiles coincide.
     * Internal class, not exposed in the public API.
     */
    class TerrainTileTransformer final : public vt::TileTransformer {
    public:
        class TerrainVertexTransformer final : public VertexTransformer {
        public:
            TerrainVertexTransformer(const vt::TileId& tileId, double scale, std::shared_ptr<ElevationTileGrid> grid, float exaggeration, float divideThreshold, float lineDivideThreshold, float latticeCell, float sagToleranceMeters);
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
            double calculateLocalHeight(const cglib::vec2<float>& pos) const;
            double calculateMercatorCosine(double internalY) const;

            void tesselateSegment(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, float dist, float threshold, vt::VertexArray<cglib::vec2<float>>& points) const;
            // Splits a segment only where the terrain under it leaves the chord, until the residual
            // sag is under the tolerance. A cut costs one elevation sample, so a valley floor is not
            // cut at all - unlike the lattice, whose cost is the cell count whatever the relief.
            void tesselateSegmentBySag(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, double h0, double h1, float dist, int depth, vt::VertexArray<cglib::vec2<float>>& points) const;
            // Splits a segment where it crosses surface cell edges and each cell's diagonal, so
            // every sub-segment lies inside ONE surface triangle. False when the segment spans too
            // many cells to be worth it, and the caller falls back to halving by threshold.
            bool tesselateSegmentOnLattice(const cglib::vec2<float>& pos0, const cglib::vec2<float>& pos1, vt::VertexArray<cglib::vec2<float>>& points) const;
            void tesselateTriangle(std::size_t i0, std::size_t i1, std::size_t i2, float dist01, float dist02, float dist12, vt::VertexArray<cglib::vec2<float>>& coords, vt::VertexArray<cglib::vec2<float>>& texCoords, vt::VertexArray<std::size_t>& indices) const;

            const vt::TileId _tileId;
            const double _scale;
            const std::shared_ptr<ElevationTileGrid> _grid;
            const float _exaggeration;
            const float _divideThreshold; // triangle subdivision, EPSG3857 meters; infinity disables subdivision
            const float _lineDivideThreshold; // line subdivision, EPSG3857 meters; finer than the triangle threshold in regular-grid mode
            const float _latticeCell; // surface grid cell size in tile-local units; 0 outside regular-grid mode
            float _sagToleranceLocal = 0.0f; // max chord sag in tile-local height units; 0 disables sag subdivision
            float _sagMinSegmentMeters = 0.0f; // never cut below the elevation data's own resolution
            cglib::vec2<double> _tileOffsetInternal; // internal coordinates of the tile origin (min x, min y)
            double _tileScaleInternal;
            double _tileScaleMeters;
            double _localFromInternal;
        };

        TerrainTileTransformer(float scale, const std::shared_ptr<ElevationManager>& elevationManager, int meshResolution, int minZoom, bool sourceDensity, bool sourceDensityLines);
        virtual ~TerrainTileTransformer() = default;

        std::shared_ptr<ElevationManager> getElevationManager() const { return _elevationManager; }
        int getMeshResolution() const { return _meshResolution; }
        int getMinZoom() const { return _minZoom; }

        virtual bool isElevationBased() const override { return true; }

        virtual cglib::vec3<double> calculateTileOrigin(const vt::TileId& tileId) const override;
        virtual cglib::bbox3<double> calculateTileBBox(const vt::TileId& tileId) const override;
        virtual cglib::mat4x4<double> calculateTileMatrix(const vt::TileId& tileId, float coordScale) const override;
        virtual cglib::mat4x4<float> calculateTileTransform(const vt::TileId& tileId, const cglib::vec2<float>& translate, float coordScale) const override;

        virtual std::shared_ptr<const VertexTransformer> createTileVertexTransformer(const vt::TileId& tileId) const override;

    private:
        static constexpr float FLAT_HEIGHT_RANGE_EPSILON = 0.001f;
        // Regular-grid draped LINES subdivide to this fraction of a surface cell (< 1 = finer) so
        // they stop chording the cell's anti-diagonal fold. Lower = fewer cracks, more vertices.
        // Triangles stay at one cell: their sag is bounded and area content costs 1/factor^2.
        static constexpr double REGULAR_GRID_LINE_SUBDIVISION = 0.35;
        // A segment crossing more cells than this is split by halving instead of by lattice
        // crossings - the crossing list would cost more than it saves.
        static constexpr int MAX_LATTICE_SPLITS_PER_SEGMENT = 64;

        // Recursion guard for the sag-driven line split: 2^10 sub-segments is far past anything a
        // real DEM asks for, and it bounds the work a pathological cliff can demand.
        static constexpr int MAX_SAG_SPLIT_DEPTH = 10;

        const double _scale;
        const std::shared_ptr<ElevationManager> _elevationManager;
        const int _meshResolution;
        const int _minZoom; // tiles below this zoom level are rendered flat
        const bool _sourceDensity; // source-density (tangram) mode: do not subdivide draped fills; GPU-displace at source density + lifting depth slack
        const bool _sourceDensityLines; // also skip line subdivision (draped lines are baked flat)
    };
}

#endif
