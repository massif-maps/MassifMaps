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
     * VBOs of one cached terrain tile mesh: client-side arrays re-copied every mesh on every draw.
     * Owned by its mesh cache entry; freed through GLResourceManager's queue, so on the GL thread.
     */
    class TerrainMeshBuffer : public GLResource {
    public:
        virtual ~TerrainMeshBuffer();

        /**
         * Uploads positions and indices. Called once, when the mesh is first drawn.
         */
        void uploadGeometry(const std::vector<float>& vertices, const std::vector<unsigned short>& indices);
        /**
         * Uploads the interleaved normal/elevation attributes: baked lazily, and re-baked when the
         * normal sample distance changes (TerrainRenderer::ensureSurfaceAttribs).
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
