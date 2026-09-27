#include "TerrainMeshBuffer.h"
#include "renderers/utils/GLResourceManager.h"

namespace massif {

    TerrainMeshBuffer::TerrainMeshBuffer(const std::weak_ptr<GLResourceManager>& manager) :
        GLResource(manager),
        _vertexVBO(0),
        _indexVBO(0),
        _attribVBO(0),
        _geometryUploaded(false),
        _attribsUploaded(false)
    {
    }

    TerrainMeshBuffer::~TerrainMeshBuffer() {
    }

    void TerrainMeshBuffer::create() {
        if (_vertexVBO != 0) {
            return;
        }
        glGenBuffers(1, &_vertexVBO);
        glGenBuffers(1, &_indexVBO);
        glGenBuffers(1, &_attribVBO);
    }

    void TerrainMeshBuffer::destroy() {
        if (_vertexVBO == 0) {
            return;
        }
        glDeleteBuffers(1, &_vertexVBO);
        glDeleteBuffers(1, &_indexVBO);
        glDeleteBuffers(1, &_attribVBO);
        _vertexVBO = _indexVBO = _attribVBO = 0;
        _geometryUploaded = _attribsUploaded = false;
    }

    void TerrainMeshBuffer::uploadGeometry(const std::vector<float>& vertices, const std::vector<unsigned short>& indices) {
        if (_vertexVBO == 0 || vertices.empty() || indices.empty()) {
            return;
        }
        glBindBuffer(GL_ARRAY_BUFFER, _vertexVBO);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexVBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned short)), indices.data(), GL_STATIC_DRAW);
        _geometryUploaded = true;
    }

    void TerrainMeshBuffer::uploadAttribs(const std::vector<float>& surfaceAttribs) {
        if (_attribVBO == 0 || surfaceAttribs.empty()) {
            return;
        }
        glBindBuffer(GL_ARRAY_BUFFER, _attribVBO);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(surfaceAttribs.size() * sizeof(float)), surfaceAttribs.data(), GL_STATIC_DRAW);
        _attribsUploaded = true;
    }

}
