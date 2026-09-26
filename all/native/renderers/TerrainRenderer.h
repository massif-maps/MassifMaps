/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINRENDERER_H_
#define _MASSIF_TERRAINRENDERER_H_

#include "components/StyleEnvironment.h"
#include "core/MapTile.h"
#include "graphics/Color.h"
#include "graphics/ViewState.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif {
    class Bitmap;
    class ElevationManager;
    namespace vt { class TileTransformer; }
    class ElevationTileGrid;
    class TerrainOptions;
    class FrameBuffer;
    class Shader;
    class Texture;
    class GLResourceManager;
    class TerrainDepthWorker;
    class ElevationTextureCache;
    struct TerrainDepthBuffer;

    /**
     * Renders the displaced terrain surface as per-tile grid meshes with skirts: as a depth pre-pass the
     * tile layers test against (one depth source, no z-fighting between layers), and as the packed depth
     * texture post-process effects read. Internal class.
     */
    class TerrainRenderer {
    public:
        TerrainRenderer();
        virtual ~TerrainRenderer();

        /**
         * The surface (plane or globe) the mesh is built on; set before any render call. A change drops
         * the meshes, whose vertices carry the shape. See docs/internals/rendering/18-globe.md.
         */
        void setTileTransformer(const std::shared_ptr<vt::TileTransformer>& tileTransformer);

    private:
        static double sphericalLocalPerInternal(const MapTile& tile, double internalY);
    public:

        /**
         * Renders terrain depth into the bound framebuffer, color writes off. Restores GL state.
         */
        bool renderDepthPrepass(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager);

        /**
         * Renders the terrain as an opaque color. keepDepth leaves its depth in place (replacing renderDepthPrepass);
         * otherwise depth is cleared so the fill cannot clip differently-tesselated tile content above it.
         * Restores GL state.
         */
        bool renderBackground(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const Color& color, bool keepDepth);

        /**
         * Bitmap variant of the color background, tiled as BackgroundRenderer does; same keepDepth semantics.
         */
        bool renderBackground(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const std::shared_ptr<Bitmap>& bitmap, bool keepDepth);

        /**
         * Paints the terrain with TerrainOptions::setSurfaceShaderSource; same keepDepth semantics.
         * Returns false when no shader is set or it fails to compile: the caller falls back to bitmap/color.
         */
        bool renderSurface(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const ResolvedLighting& lighting, const ResolvedFog& fog, bool keepDepth);

        /**
         * Renders the packed terrain depth texture; restores the previous framebuffer binding.
         * meshResolutionCap 0 is full resolution: a line effect draws every coarse mesh edge as a fold.
         * withNormals: the PostProcessEffect::setTerrainNormalsRequired layout.
         */
        bool renderDepthTexture(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, int meshResolutionCap = DEPTH_TEXTURE_MESH_RESOLUTION, bool withNormals = false, bool forReadback = false);

        /**
         * Shared with the tile renderer, set once per frame. Lets the surface and normal passes sample the DEM
         * per fragment (as geo-three); without it they use the per-vertex mesh normal.
         */
        void setElevationTextureCache(const std::shared_ptr<ElevationTextureCache>& cache) { _elevationTextureCache = cache; }

        /**
         * Returns the GL texture id of the packed depth buffer (0 if not rendered).
         */
        unsigned int getDepthTextureId() const;

        /**
         * Renders the depth texture and reads it back to the CPU for occlusion queries. With an offscreen
         * context this runs on TerrainDepthWorker and lands a frame or two later; otherwise here, throttled
         * while the camera moves.
         */
        bool updateDepthBuffer(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager);

        /**
         * True when a deferred update left the occlusion depth behind the camera; the caller must keep
         * requesting frames so the refresh happens once the camera settles.
         */
        bool isDepthBufferStale() const { return _depthStale; }

        /**
         * True when pos is behind the terrain beyond the relative tolerance (1 = no slack). Projected with the
         * buffer's own camera, which lags a moving one: current-camera depths would occlude every label.
         * Off-buffer positions reuse their last verdict; fails open without one (`answered` says which).
         */
        bool isOccludedByTerrain(const cglib::vec3<double>& pos, float tolerance, bool* answered = nullptr) const;

        /** Bumped each time a new occlusion depth is published: the verdicts may have changed. */
        unsigned int getDepthSnapshotVersion() const { return _depthSnapshotVersion.load(); }

        /**
         * The tiles the surface would be drawn from, for consumers with no tile set of their own.
         */
        void collectVisibleTiles(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, std::vector<MapTile>& tiles) const;

    private:
        struct TileMesh;
        struct MeshCacheEntry;

        std::shared_ptr<ElevationTextureCache> _elevationTextureCache;
        // Own buffer: its size differs from the post-process one (setPostProcessDownscale).
        std::shared_ptr<FrameBuffer> _readbackFrameBuffer;

        static constexpr int BUFFER_DOWNSCALE = 2;    // occlusion read-back buffer, half resolution
        // glReadPixels stalls the pipeline; a lagging occlusion depth is invisible, a stalled frame is not.
        static constexpr int DEPTH_READBACK_THROTTLE = 60;        // minimum interval (ms) between read-backs
        static constexpr int DEPTH_READBACK_MOVING_INTERVAL = 500; // ...while the camera keeps moving
        // The async worker's GL context still contends for the GPU with the render one.
        static constexpr int DEPTH_SUBMIT_MOVING_INTERVAL = 500;   // minimum interval (ms) between worker jobs while moving
        // One log line per mesh build; never ship it on.
        static constexpr bool TERRAIN_MESH_TRACE = false;

        // Metres, not tile-local z: the crack it covers is bounded by the local relief. See buildTileMesh.
        static constexpr double SKIRT_DEPTH_METERS = 500.0;
        static constexpr int MIN_MESH_GRID_SIZE = 4;  // grid cells per tile edge, lower bound
        static constexpr int MAX_MESH_GRID_SIZE = 96; // grid cells per tile edge, upper bound
        // geo-three's mesh (setSubdivideDistance): full MeshResolution up to this zoom, halved per level above.
        static constexpr int REFERENCE_MESH_FULL_ZOOM = 12;
        static constexpr int REFERENCE_MIN_MESH_GRID_SIZE = 16;
        // The most cells per edge a mesh indexed with 16 bits holds: (n + 1)^2 grid vertices and 8n skirt
        // vertices must stay under 65536, or the indices wrap and triangles join the wrong vertices.
        static constexpr int MAX_INDEXED_MESH_GRID_SIZE = 250;
        static constexpr int MAX_CACHED_MESHES = 160;
        // Half the cache: each pass of a frame caches its own resolution, and must not evict the next pass's.
        static constexpr int MAX_VISIBLE_MESH_TILES = MAX_CACHED_MESHES / 2;
        static constexpr int DEPTH_TEXTURE_MESH_RESOLUTION = 32; // mesh cap for the occlusion depth texture
        static constexpr int OCCLUSION_SAMPLE_OFFSET = 4; // buffer pixels sampled around a queried position
        // Past it the whole table is dropped: a verdict is only a hint.
        static constexpr std::size_t MAX_OCCLUSION_VERDICTS = 8192;
        // Bounded so the worker does not lag long after the camera has moved on.
        static constexpr std::size_t MAX_PENDING_ATTRIB_JOBS = 64;
        // Re-bakes as better DEM arrives; the cap stops a tile with no source data retrying on every insert.
        static constexpr int MAX_ATTRIB_REBAKES = 4;

        static const std::string TERRAIN_DEPTH_VERTEX_SHADER;
        static const std::string TERRAIN_DEPTH_FRAGMENT_SHADER;
        static const std::string TERRAIN_NORMAL_DEPTH_VERTEX_SHADER;
        static const std::string TERRAIN_NORMAL_DEPTH_FRAGMENT_SHADER;
        static const std::string TERRAIN_COLOR_FRAGMENT_SHADER;
        static const std::string TERRAIN_BITMAP_VERTEX_SHADER;
        static const std::string TERRAIN_BITMAP_FRAGMENT_SHADER;
        static const std::string TERRAIN_SURFACE_VERTEX_SHADER;
        static const std::string TERRAIN_SURFACE_FRAGMENT_SHADER_PREFIX;
        static const std::string TERRAIN_SURFACE_FRAGMENT_SHADER_MAIN;

        // meshResolutionCap > 0 caps the grid for point-sampled passes. normalAttrib binds a_normal outside a
        // surface pass. No caller sets skipSkirts: the skirts still cover real cracks.
        bool renderTiles(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const std::shared_ptr<Shader>& shader, const std::function<void(const MapTile&)>& tileUniformsFn = std::function<void(const MapTile&)>(), int meshResolutionCap = 0, bool surfaceAttribs = false, bool normalAttrib = false, bool skipSkirts = false);
        // A source that failed once is not retried until it changes.
        std::shared_ptr<Shader> updateSurfaceShader(const std::string& shaderSource, const std::string& fogShaderSource, const std::shared_ptr<GLResourceManager>& glResourceManager);
        // Lazy normal + elevation (metres) per vertex, surface pass only.
        // normalSampleDistance: 0 takes the gradient from the mesh, else from the DEM at that many metres.
        void ensureSurfaceAttribs(const MapTile& tile, const std::shared_ptr<ElevationManager>& elevationManager, TileMesh& mesh, float normalSampleDistance, bool allowFixedScale) const;
        // Shared by the render path and the offscreen depth job so both draw the same terrain.
        void collectTileMeshes(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, int meshResolutionCap, std::vector<std::pair<MapTile, std::shared_ptr<TileMesh> > >& tileMeshes);
        // Spares everything the current pass already drew.
        void evictLeastRecentlyUsedMeshes(unsigned int pass, int maxCachedMeshes);

        // DEM-sampled normals, baked off the render thread; the grids are immutable, so no lock.
        void startAttribWorker();
        void stopAttribWorker();
        void queueAttribRefine(const MapTile& tile, const std::shared_ptr<ElevationManager>& elevationManager, const std::shared_ptr<TileMesh>& mesh, float normalSampleDistance);
        void applyRefinedAttribs();
        bool updateDepthBufferAsync(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions);
        bool updateDepthBufferSync(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager);
        // Const::MAX_SUPPORTED_ZOOM_LEVEL as `maxZoom` means no cap.
        void calculateVisibleTiles(const ViewState& viewState, const std::shared_ptr<ElevationManager>& elevationManager, const MapTile& tile, int maxZoom, float subdivideDistance, std::vector<MapTile>& tiles) const;
        // edgeHeights: per side (south, north, west, east; gy = 0 south), internal units, empty keeps the tile's own.
        std::shared_ptr<TileMesh> buildTileMesh(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, const std::shared_ptr<ElevationManager>& elevationManager, int gridSize, const std::array<std::vector<double>, 4>& edgeHeights, bool bilinearHeights) const;
        int calculateMeshGridSize(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, int meshResolution, bool fixedScaleNormals, bool referenceMesh) const;
        cglib::mat4x4<double> calculateTileMatrix(const MapTile& tile) const;
        // Linear eye depth (view w, internal units); huge for sky and out-of-buffer pixels.
        static float sampleDepthW(const TerrainDepthBuffer& depthData, int x, int y);
        // Horizontal position only: a label's elevation is re-anchored while elevation tiles stream in.
        static long long occlusionVerdictKey(const cglib::vec3<double>& pos);
        bool cachedOcclusionVerdict(long long key) const;
        void rememberOcclusionVerdict(long long key, bool occluded) const;
        void resetOcclusionVerdicts();

        std::shared_ptr<vt::TileTransformer> _tileTransformer;
        std::shared_ptr<FrameBuffer> _frameBuffer;
        std::shared_ptr<Shader> _shader;
        std::shared_ptr<Shader> _normalShader; // the normal-packing variant of the depth pass
        std::shared_ptr<Shader> _colorShader;
        std::shared_ptr<Shader> _bitmapShader;
        std::shared_ptr<Shader> _surfaceShader;
        std::string _surfaceShaderSource;    // source _surfaceShader was built from
        std::string _fogShaderSource;        // fog block compiled into _surfaceShader
        bool _surfaceShaderFailed = false;   // that source does not compile: do not retry every frame
        std::chrono::steady_clock::time_point _startTime = std::chrono::steady_clock::now(); // u_time origin
        // Cache key of the packed depth texture.
        cglib::mat4x4<double> _depthTextureMVPMatrix = cglib::mat4x4<double>::zero();
        unsigned int _depthTextureElevationVersion = 0;
        int _depthTextureMeshResolutionCap = -1;
        bool _depthTextureWithNormals = false; // part of the key: the two layouts are not interchangeable
        std::shared_ptr<Bitmap> _backgroundBitmap; // source of _backgroundTex, for change detection
        std::shared_ptr<Texture> _backgroundTex;
        // Keyed by (tile id, grid size): the occlusion pass draws the same tiles coarser.
        std::map<std::pair<long long, int>, MeshCacheEntry> _meshCache;
        unsigned int _meshCacheClock = 0; // incremented per collectTileMeshes pass; stamps MeshCacheEntry::lastUsed

        struct AttribJob {
            MapTile tile = MapTile(0, 0, 0, 0);
            std::shared_ptr<ElevationManager> elevationManager;
            std::shared_ptr<TileMesh> mesh;
            float normalSampleDistance = 0;
        };
        struct AttribResult {
            std::shared_ptr<TileMesh> mesh;
            std::vector<float> attribs;
            float normalSampleDistance = 0;
            int attribsDemZoom = -1;
            // Baked on a scratch copy, so the DEM state travels back with the attribs.
            bool attribsProvisional = false;
            int attribsWorstZoom = -1;
            unsigned int attribsDataVersion = 0;
        };
        std::thread _attribWorker;
        std::mutex _attribMutex;
        std::condition_variable _attribCondition;
        std::deque<AttribJob> _attribJobs;
        std::vector<AttribResult> _attribResults;
        std::atomic<bool> _attribWorkerStop { false };
        // A refine changes none of the depth texture's cache key, so it must invalidate it explicitly.
        bool _depthTextureAttribsDirty = false;

        // The budgeted terrain cut, memoised for the frame: every pass asks for it.
        mutable std::mutex _visibleTilesMutex;
        mutable cglib::mat4x4<double> _visibleTilesMVP = cglib::mat4x4<double>::zero();
        mutable unsigned int _visibleTilesElevationVersion = 0;
        mutable float _visibleTilesSubdivideDistance = 0.0f;
        mutable std::vector<MapTile> _visibleTilesCache;
        mutable bool _visibleTilesValid = false;
        // The zoom the last cut settled on: the budget loop starts there.
        mutable int _budgetMaxZoom = 24;

        // Published as a whole immutable snapshot: the label placement worker never sees half a read-back.
        std::unique_ptr<TerrainDepthWorker> _depthWorker;
        std::shared_ptr<const TerrainDepthBuffer> _depthDataSnapshot;
        std::atomic<unsigned int> _depthSnapshotVersion{0};
        mutable std::mutex _depthMutex;
        cglib::mat4x4<double> _depthMVPMatrix = cglib::mat4x4<double>::zero(); // camera state of the last read-back
        unsigned int _depthElevationVersion = 0;
        std::chrono::steady_clock::time_point _depthReadbackTime; // throttles read-backs while the camera moves
        cglib::mat4x4<double> _depthLastSeenMVPMatrix = cglib::mat4x4<double>::zero(); // camera of the previous frame
        bool _depthStale = false; // an update was deferred: the data no longer matches the camera

        // The lagging depth buffer cannot see a label entering from the screen edge; it keeps its last
        // verdict instead of blinking visible. Cleared when the elevation changes.
        mutable std::unordered_map<long long, bool> _occlusionVerdicts;
        mutable std::mutex _occlusionVerdictMutex;
    };
}

#endif
