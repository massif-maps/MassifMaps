/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINMESHBUFFER_H_
#define _MASSIF_TERRAINMESHBUFFER_H_

#include "renderers/utils/GLResource.h"

#include <vector>

namespace massif {

    /**
     * The GPU side of one cached terrain tile mesh: positions, indices and the surface attributes.
     *
     * TerrainRenderer used to draw its meshes straight out of the std::vectors they are built in -
     * client-side vertex arrays, which means the driver copies the whole mesh across the bus before
     * every draw call. A panorama draws ~80 tiles at 97x97 vertices, so that was roughly 24 MB a
     * frame, and it measured 116 ms of a 130 ms frame on an Adreno 610 (`PROF PRELUDE`, surface).
     * The meshes are CACHED and change only when their tile is rebuilt, so the copy bought nothing.
     *
     * Lifetime rides on the mesh cache entry that owns it. Destruction goes through
     * GLResourceManager's delete queue, so a mesh evicted off the GL thread still frees its buffers
     * on it.
     */
    class TerrainMeshBuffer : public GLResource {
    public:
        virtual ~TerrainMeshBuffer();

        /**
         * Uploads positions and indices. Called once, when the mesh is first drawn.
         */
        void uploadGeometry(const std::vector<float>& vertices, const std::vector<unsigned short>& indices);
        /**
         * Uploads the interleaved normal/elevation attributes. Separate from the geometry because
         * they are baked lazily on first use and re-baked when TerrainOptions' normal sample
         * distance changes - see TerrainRenderer::ensureSurfaceAttribs.
         */
        void uploadAttribs(const std::vector<float>& surfaceAttribs);

        bool hasGeometry() const { return _geometryUploaded; }
        bool hasAttribs() const { return _attribsUploaded; }

        unsigned int getVertexVBO() const { return _vertexVBO; }
        unsigned int getIndexVBO() const { return _indexVBO; }
        unsigned int getAttribVBO() const { return _attribVBO; }

    protected:
        friend class GLResourceManager;

        explicit TerrainMeshBuffer(const std::weak_ptr<GLResourceManager>& manager);

        virtual void create();
        virtual void destroy();

    private:
        unsigned int _vertexVBO;
        unsigned int _indexVBO;
        unsigned int _attribVBO;
        bool _geometryUploaded;
        bool _attribsUploaded;
    };

}

#endif
