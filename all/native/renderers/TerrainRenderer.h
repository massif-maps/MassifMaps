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

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif {
    class Bitmap;
    class ElevationManager;
    class ElevationTileGrid;
    class TerrainOptions;
    class FrameBuffer;
    class Shader;
    class Texture;
    class GLResourceManager;
    class TerrainDepthWorker;
    struct TerrainDepthBuffer;

    /**
     * Renders the displaced terrain surface as per-tile grid meshes (with skirts).
     * Used in two ways:
     * 1. renderDepthPrepass: renders terrain depth into the currently bound framebuffer
     *    (color writes disabled) before the tile layers are drawn. The 2D tile geometry
     *    then depth-tests against this single consistent depth source (with a small bias),
     *    which gives terrain self-occlusion without z-fighting between layers.
     * 2. renderDepthTexture: renders packed 24-bit linear depth (RGB, relative to the
     *    far plane) plus terrain coverage (A) into a half-resolution offscreen buffer,
     *    consumed by post-process effects.
     * Internal class, not exposed in the public API.
     */
    class TerrainRenderer {
    public:
        TerrainRenderer();
        virtual ~TerrainRenderer();

        /**
         * Renders terrain depth into the currently bound framebuffer. Color writes are
         * disabled during the pass and GL state is restored on return. Returns true on success.
         */
        bool renderDepthPrepass(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager);

        /**
         * Renders the terrain surface as an opaque solid color into the currently bound
         * framebuffer. The fill is always depth-resolved internally (near slopes win over
         * far slopes). With keepDepth, the terrain depth stays in the depth buffer and
         * subsumes renderDepthPrepass (used when no tile layer provides the terrain
         * depth). Without keepDepth the depth buffer is cleared afterwards: the fill is
         * color-only and can not depth-clip the differently-tesselated tile layer
         * content drawn above it - the tile layer surface pre-passes provide the depth.
         * GL state is restored on return. Returns true on success.
         */
        bool renderBackground(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const Color& color, bool keepDepth);

        /**
         * Renders the terrain surface with the given repeating background bitmap draped
         * over it (the same world-anchored tiling the flat-map BackgroundRenderer uses) -
         * the bitmap variant of the color background, with the same keepDepth semantics.
         * GL state is restored on return. Returns true on success.
         */
        bool renderBackground(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const std::shared_ptr<Bitmap>& bitmap, bool keepDepth);

        /**
         * Renders the terrain surface painted by the application's surface shader
         * (TerrainOptions::setSurfaceShaderSource) - the shaded variant of the color/bitmap
         * background, with the same keepDepth semantics. The surface normal, elevation and
         * camera distance are supplied per vertex, the sun and fog per frame. Returns false
         * when no shader is set or it does not compile, in which case the caller falls back
         * to the bitmap/color background. GL state is restored on return.
         */
        bool renderSurface(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const ResolvedLighting& lighting, const ResolvedFog& fog, bool keepDepth);

        /**
         * Renders the packed terrain depth texture for post-processing. Returns true on success.
         * Leaves the previously bound framebuffer bound again on return.
         * meshResolutionCap 0 draws the terrain at its full mesh resolution: an effect that draws
         * LINES from this depth sees every mesh edge as a fold, so a coarser mesh than the one on
         * screen is not an approximation there, it is the pattern it draws. The occlusion
         * read-back, which only samples points, keeps the cheap cap.
         */
        bool renderDepthTexture(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, int meshResolutionCap = DEPTH_TEXTURE_MESH_RESOLUTION);

        /**
         * Returns the GL texture id of the packed depth buffer (0 if not rendered).
         */
        unsigned int getDepthTextureId() const;

        /**
         * Renders the terrain depth texture and reads it back into a CPU buffer for
         * pixel-exact occlusion queries (getDepthW). Returns true on success.
         *
         * Where an offscreen GL context is available the render and the read-back happen on
         * the TerrainDepthWorker thread and this call only collects the meshes to draw - the
         * data then lands a frame or two later. Otherwise both happen here, and the read-back
         * stall is kept tolerable by only refreshing at a coarse interval while the camera moves.
         */
        bool updateDepthBuffer(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager);

        /**
         * True when the occlusion depth data no longer matches the camera because the
         * update was deferred while the camera moves. The caller must keep asking for
         * frames while this holds, so that the refresh happens once the camera settles.
         */
        bool isDepthBufferStale() const { return _depthStale; }

        /**
         * True when the given world position is behind the terrain, by more than the given
         * relative depth tolerance (1 = no slack).
         *
         * The position is projected with the camera the depth buffer was RENDERED from, not
         * with the current one: the buffer lags a moving camera by up to the submit interval,
         * so a current-camera distance compared against it reads every label as occluded while
         * zooming out. Projecting with the buffer's own matrix makes the answer merely late.
         * Fails open (not occluded) when there is no data, or when the position falls behind
         * that camera or outside its viewport.
         */
        bool isOccludedByTerrain(const cglib::vec3<double>& pos, float tolerance) const;

        /**
         * The terrain tile cover for this camera - the tiles the surface would be drawn from.
         * For consumers that need ground to draw on without having a tile set of their own
         * (a terrain paint layer with no vector layer under it).
         */
        void collectVisibleTiles(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, std::vector<MapTile>& tiles) const;

    private:
        struct TileMesh;
        struct MeshCacheEntry;

        static constexpr int BUFFER_DOWNSCALE = 2;    // packed depth texture runs at half resolution
        // The occlusion read-back is a glReadPixels, i.e. a full pipeline stall: measured on an
        // Adreno 610 at 55-62 ms (peaks 134 ms) on top of the ~20 ms depth render. Running that
        // every 60 ms while the camera moves costs more than the whole rest of the frame, so
        // while it moves the data is only refreshed at a coarse interval and the exact refresh
        // is done once the camera settles - the occlusion depth is allowed to lag a gesture,
        // which is invisible (billboards fade), but a stalled frame is not.
        static constexpr int DEPTH_READBACK_THROTTLE = 60;        // minimum interval (ms) between read-backs
        static constexpr int DEPTH_READBACK_MOVING_INTERVAL = 500; // ...while the camera keeps moving
        // The asynchronous path has no stall to pay for, but its second GL context still shares
        // the GPU with the render context, and on an Adreno 610 that contention is what the
        // interval buys back: measured at mesh 64 with occlusion on, 100 ms costs 13.3 fps
        // (prelude 18-22 ms), 250 ms 14.3 fps (prelude 8-10), 500 ms 14.9 fps (prelude 3-7) -
        // against 13.7 fps (prelude 12-14) for the synchronous read-back at the same cadence.
        static constexpr int DEPTH_SUBMIT_MOVING_INTERVAL = 500;   // minimum interval (ms) between worker jobs while moving
        static constexpr int MIN_MESH_GRID_SIZE = 4;  // grid cells per tile edge, lower bound
        static constexpr int MAX_MESH_GRID_SIZE = 96; // grid cells per tile edge, upper bound
        static constexpr int MAX_CACHED_MESHES = 160;
        static constexpr int DEPTH_TEXTURE_MESH_RESOLUTION = 32; // mesh cap for the occlusion depth texture
        static constexpr int OCCLUSION_SAMPLE_OFFSET = 4; // buffer pixels sampled around a queried position

        static const std::string TERRAIN_DEPTH_VERTEX_SHADER;
        static const std::string TERRAIN_DEPTH_FRAGMENT_SHADER;
        static const std::string TERRAIN_COLOR_FRAGMENT_SHADER;
        static const std::string TERRAIN_BITMAP_VERTEX_SHADER;
        static const std::string TERRAIN_BITMAP_FRAGMENT_SHADER;
        static const std::string TERRAIN_SURFACE_VERTEX_SHADER;
        static const std::string TERRAIN_SURFACE_FRAGMENT_SHADER_PREFIX;
        static const std::string TERRAIN_SURFACE_FRAGMENT_SHADER_MAIN;

        // meshResolutionCap > 0 caps the per-tile mesh grid below what TerrainOptions asks
        // for. The occlusion depth texture is a half-resolution approximation sampled at
        // single points, so it does not need the full render mesh - and that mesh is CPU
        // built and drawn from client memory, which is the expensive part of the pass.
        bool renderTiles(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager, const std::shared_ptr<Shader>& shader, const std::function<void(const MapTile&)>& tileUniformsFn = std::function<void(const MapTile&)>(), int meshResolutionCap = 0, bool surfaceAttribs = false);
        // Compiles (and caches) the surface program for the current TerrainOptions shader source.
        // A source that failed once is not retried until it changes.
        std::shared_ptr<Shader> updateSurfaceShader(const std::string& shaderSource, const std::string& fogShaderSource, const std::shared_ptr<GLResourceManager>& glResourceManager);
        // Fills the mesh's per-vertex surface attributes (normal + elevation in metres) on first
        // use. Only the surface pass needs them, so the depth passes never pay for them.
        void ensureSurfaceAttribs(const MapTile& tile, const std::shared_ptr<ElevationManager>& elevationManager, TileMesh& mesh) const;
        // Visible tiles paired with their (cached, built here if missing) meshes. Both the
        // rendering path and the offscreen depth job start from this, so they always draw the
        // same terrain.
        void collectTileMeshes(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, int meshResolutionCap, std::vector<std::pair<MapTile, std::shared_ptr<TileMesh> > >& tileMeshes);
        // Drops the oldest meshes until the cache is back under its cap, sparing everything the
        // current pass already drew.
        void evictLeastRecentlyUsedMeshes(unsigned int pass);
        bool updateDepthBufferAsync(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions);
        bool updateDepthBufferSync(const ViewState& viewState, const std::shared_ptr<TerrainOptions>& terrainOptions, const std::shared_ptr<GLResourceManager>& glResourceManager);
        void calculateVisibleTiles(const ViewState& viewState, const std::shared_ptr<ElevationManager>& elevationManager, const MapTile& tile, std::vector<MapTile>& tiles) const;
        std::shared_ptr<TileMesh> buildTileMesh(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, const std::shared_ptr<ElevationManager>& elevationManager, int gridSize) const;
        int calculateMeshGridSize(const MapTile& tile, const std::shared_ptr<ElevationTileGrid>& grid, int meshResolution) const;
        cglib::mat4x4<double> calculateTileMatrix(const MapTile& tile) const;
        // Linear eye depth (view w, internal units) of the terrain at a buffer pixel. Returns a
        // huge value for sky pixels and for pixels outside the buffer.
        static float sampleDepthW(const TerrainDepthBuffer& depthData, int x, int y);

        std::shared_ptr<FrameBuffer> _frameBuffer;
        std::shared_ptr<Shader> _shader;
        std::shared_ptr<Shader> _colorShader;
        std::shared_ptr<Shader> _bitmapShader;
        std::shared_ptr<Shader> _surfaceShader;
        std::string _surfaceShaderSource;    // source _surfaceShader was built from
        std::string _fogShaderSource;        // ... and the fog block compiled into it
        bool _surfaceShaderFailed = false;   // that source does not compile: do not retry every frame
        std::chrono::steady_clock::time_point _startTime = std::chrono::steady_clock::now(); // u_time origin
        // What the packed depth texture currently holds: it is reused while the camera, the
        // elevation and the mesh cap are unchanged (see renderDepthTexture).
        cglib::mat4x4<double> _depthTextureMVPMatrix = cglib::mat4x4<double>::zero();
        unsigned int _depthTextureElevationVersion = 0;
        int _depthTextureMeshResolutionCap = -1;
        std::shared_ptr<Bitmap> _backgroundBitmap; // source of _backgroundTex, for change detection
        std::shared_ptr<Texture> _backgroundTex;
        // Keyed by (tile id, mesh grid size): the occlusion depth texture draws the same
        // tiles at a coarser grid than the rendered terrain, and a tile-only key would make
        // the two passes rebuild every mesh in turn.
        std::map<std::pair<long long, int>, MeshCacheEntry> _meshCache;
        unsigned int _meshCacheClock = 0; // incremented per collectTileMeshes pass; stamps MeshCacheEntry::lastUsed

        // The occlusion depth is written by whichever path produced it and read by the label
        // placement worker, so it is published as a whole immutable snapshot: a reader either
        // sees the previous read-back or the new one, never half of each.
        std::unique_ptr<TerrainDepthWorker> _depthWorker;
        std::shared_ptr<const TerrainDepthBuffer> _depthDataSnapshot;
        mutable std::mutex _depthMutex;
        cglib::mat4x4<double> _depthMVPMatrix = cglib::mat4x4<double>::zero(); // camera state of the last read-back
        unsigned int _depthElevationVersion = 0;
        std::chrono::steady_clock::time_point _depthReadbackTime; // throttles read-backs while the camera moves
        cglib::mat4x4<double> _depthLastSeenMVPMatrix = cglib::mat4x4<double>::zero(); // camera of the previous frame
        bool _depthStale = false; // an update was deferred: the data no longer matches the camera
    };
}

#endif
