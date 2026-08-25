#include "PolygonRenderer.h"
#include "graphics/Bitmap.h"
#include "graphics/ViewState.h"
#include "layers/VectorLayer.h"
#include "renderers/MapRenderer.h"
#include "renderers/drawdatas/LineDrawData.h"
#include "renderers/drawdatas/PolygonDrawData.h"
#include "renderers/components/RayIntersectedElement.h"
#include "renderers/utils/FogShader.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/Shader.h"
#include "utils/Const.h"
#include "utils/Log.h"
#include "vectorelements/Polygon.h"

#include <cglib/mat.h>

namespace massif {

    PolygonRenderer::PolygonRenderer() :
        _mapRenderer(),
        _elements(),
        _tempElements(),
        _drawDataBuffer(),
        _prevBitmap(nullptr),
        _colorBuf(),
        _coordBuf(),
        _indexBuf(),
        _shader(),
        _a_color(0),
        _a_coord(0),
        _u_mvpMat(0),
        _u_depthBias(0),
        _u_depthBiasClip(0),
        _depthBias(0.0f),
        _depthBiasClip(0.0f),
        _lineRenderer(),
        _mutex()
    {
    }
    
    PolygonRenderer::~PolygonRenderer() {
    }
    
    void PolygonRenderer::setComponents(const std::weak_ptr<Options>& options, const std::weak_ptr<MapRenderer>& mapRenderer) {
        std::lock_guard<std::mutex> lock(_mutex);

        _lineRenderer.setComponents(options, mapRenderer);
        _mapRenderer = mapRenderer;
        _shader.reset();
    }
    
    void PolygonRenderer::offsetLayerHorizontally(double offset) {
        // Offset current draw data batch horizontally by the required amount
        std::lock_guard<std::mutex> lock(_mutex);
    
        for (const std::shared_ptr<Polygon>& element : _elements) {
            element->getDrawData()->offsetHorizontally(offset);
        }

        _lineRenderer.offsetLayerHorizontally(offset);
    }
    
    void PolygonRenderer::onDrawFrame(float deltaSeconds, const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (_elements.empty()) {
            // Early return, to avoid calling glUseProgram etc.
            return;
        }

        if (!initializeRenderer()) {
            return;
        }

        glDisable(GL_CULL_FACE);
       
        bind(viewState);
    
        // Draw, batch polygons with the same bitmap and no line style
        for (const std::shared_ptr<Polygon>& element : _elements) {
            addToBatch(element->getDrawData(), viewState);
        }
        drawBatch(viewState);
        
        unbind();

        glEnable(GL_CULL_FACE);
    
        GLContext::CheckGLError("PolygonRenderer::onDrawFrame");
    }
    
    void PolygonRenderer::addElement(const std::shared_ptr<Polygon>& element) {
        if (element->getDrawData()) {
            _tempElements.push_back(element);
        }
    }
    
    void PolygonRenderer::refreshElements() {
        std::lock_guard<std::mutex> lock(_mutex);
        _elements.clear();
        _elements.swap(_tempElements);
    }
        
    void PolygonRenderer::updateElement(const std::shared_ptr<Polygon>& element) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (std::find(_elements.begin(), _elements.end(), element) == _elements.end()) {
            if (element->getDrawData()) {
                _elements.push_back(element);
            }
        }
    }
    
    void PolygonRenderer::removeElement(const std::shared_ptr<Polygon>& element) {
        std::lock_guard<std::mutex> lock(_mutex);
        _elements.erase(std::remove(_elements.begin(), _elements.end(), element), _elements.end());
    }
    
    void PolygonRenderer::calculateRayIntersectedElements(const std::shared_ptr<VectorLayer>& layer, const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);
    
        for (const std::shared_ptr<Polygon>& element : _elements) {
            FindElementRayIntersection(element, element->getDrawData(), layer, ray, viewState, results);
        }
    }
    
    void PolygonRenderer::BuildAndDrawBuffers(GLuint a_color,
                                              GLuint a_coord,
                                              std::vector<unsigned char>& colorBuf,
                                              std::vector<float>& coordBuf,
                                              std::vector<unsigned short>& indexBuf,
                                              std::vector<std::shared_ptr<PolygonDrawData> >& drawDataBuffer,
                                              const ViewState& viewState)
    {
        // Calculate buffer size
        std::size_t totalCoordCount = 0;
        std::size_t totalIndexCount = 0;
        for (const std::shared_ptr<PolygonDrawData>& drawData : drawDataBuffer) {
            for (std::size_t i = 0; i < drawData->getCoords().size(); i++) {
                const std::vector<cglib::vec3<double> >& coords = drawData->getCoords()[i];
                const std::vector<unsigned int>& indices = drawData->getIndices()[i];
                totalCoordCount += coords.size();
                totalIndexCount += indices.size();
            }
        }
    
        // Resize the buffers, if necessary
        if (coordBuf.size() < totalCoordCount * 3) {
            colorBuf.resize(std::min(totalCoordCount * 4, GLContext::MAX_VERTEXBUFFER_SIZE * 4));
            coordBuf.resize(std::min(totalCoordCount * 3, GLContext::MAX_VERTEXBUFFER_SIZE * 3));
        }
    
        if (indexBuf.size() < totalIndexCount) {
            indexBuf.resize(std::min(totalIndexCount, GLContext::MAX_VERTEXBUFFER_SIZE));
        }
    
        // View state specific data
        cglib::vec3<double> cameraPos = viewState.getCameraPos();
        std::size_t colorIndex = 0;
        std::size_t coordIndex = 0;
        std::size_t indexIndex = 0;
        for (const std::shared_ptr<PolygonDrawData>& drawData : drawDataBuffer) {
            // Draw data vertex info may be split into multiple buffers, draw each one
            for (std::size_t i = 0; i < drawData->getCoords().size(); i++) {
                // Check for possible overflow in the buffers
                const std::vector<cglib::vec3<double> >& coords = drawData->getCoords()[i];
                const std::vector<unsigned int>& indices = drawData->getIndices()[i];
                if (indices.size() > GLContext::MAX_VERTEXBUFFER_SIZE) {
                    Log::Error("PolygonRenderer::BuildAndDrawBuffers: Maximum buffer size exceeded, polygon can't be drawn");
                    continue;
                }
                if (indexIndex + indices.size() > GLContext::MAX_VERTEXBUFFER_SIZE) {
                    // If it doesn't fit, stop and draw the buffers
                    glVertexAttribPointer(a_coord, 3, GL_FLOAT, GL_FALSE, 0, coordBuf.data());
                    glVertexAttribPointer(a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colorBuf.data());
                    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexIndex), GL_UNSIGNED_SHORT, indexBuf.data());

                    // Start filling buffers from the beginning
                    colorIndex = 0;
                    coordIndex = 0;
                    indexIndex = 0;
                }
                
                // Indices
                unsigned short indexOffset = static_cast<unsigned short>(coordIndex / 3); // invariant: indexOffset <= indexIndex
                for (unsigned short index : indices) {
                    indexBuf[indexIndex] = indexOffset + index;
                    indexIndex++;
                }
                
                // Colors and coords
                const Color& color = drawData->getColor();
                for (const cglib::vec3<double>& pos : coords) {
                    colorBuf[colorIndex + 0] = color.getR();
                    colorBuf[colorIndex + 1] = color.getG();
                    colorBuf[colorIndex + 2] = color.getB();
                    colorBuf[colorIndex + 3] = color.getA();
                    colorIndex += 4;
                    
                    coordBuf[coordIndex + 0] = static_cast<float>(pos(0) - cameraPos(0));
                    coordBuf[coordIndex + 1] = static_cast<float>(pos(1) - cameraPos(1));
                    coordBuf[coordIndex + 2] = static_cast<float>(pos(2) - cameraPos(2));
                    coordIndex += 3;
                }
            }
        }
        
        // Draw buffers
        if (indexIndex > 0) {
            glVertexAttribPointer(a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colorBuf.data());
            glVertexAttribPointer(a_coord, 3, GL_FLOAT, GL_FALSE, 0, coordBuf.data());
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexIndex), GL_UNSIGNED_SHORT, indexBuf.data());
        }
    }
    
    bool PolygonRenderer::FindElementRayIntersection(const std::shared_ptr<VectorElement>& element,
                                                     const std::shared_ptr<PolygonDrawData>& drawData,
                                                     const std::shared_ptr<VectorLayer>& layer,
                                                     const cglib::ray3<double>& ray,
                                                     const ViewState& viewState,
                                                     std::vector<RayIntersectedElement>& results)
    {
        // Bounding box check
        if (!cglib::intersect_bbox(drawData->getBoundingBox(), ray)) {
            return false;
        }
        
        // Test triangles
        for (std::size_t i = 0; i < drawData->getCoords().size(); i++) {
            const std::vector<cglib::vec3<double> >& coords = drawData->getCoords()[i];
            const std::vector<unsigned int>& indices = drawData->getIndices()[i];
            
            for (std::size_t i = 0; i < indices.size(); i += 3) {
                double t = 0;
                if (cglib::intersect_triangle(coords[indices[i + 0]], coords[indices[i + 1]], coords[indices[i + 2]], ray, &t)) {
                    results.push_back(RayIntersectedElement(std::static_pointer_cast<VectorElement>(element), layer, ray(t), ray(t), layer->isZBuffering()));
                    return true;
                }
            }
        }
        return false;
    }
    
    void PolygonRenderer::setDepthBias(float depthBias, float depthBiasClip) {
        _depthBias = depthBias;
        _depthBiasClip = depthBiasClip;
    }

    bool PolygonRenderer::initializeRenderer() {
        // The custom fog shader is compiled into this program, so a change to it has to rebuild.
        std::string fogSource;
        if (auto mapRenderer = _mapRenderer.lock()) {
            fogSource = FogShader::source(mapRenderer->getOptions());
        }

        if (_shader && _shader->isValid() && _fogShaderSource == fogSource && _lineRenderer.initializeRenderer()) {
            return true;
        }
        _fogShaderSource = fogSource;

        // Null once the surface is gone - a frame still in flight has nothing to create into (#178).
        std::shared_ptr<GLResourceManager> glResourceManager;
        if (auto mapRenderer = _mapRenderer.lock()) {
            glResourceManager = mapRenderer->getGLResourceManager();
        }
        if (glResourceManager) {
            _shader = glResourceManager->create<Shader>("polygon", POLYGON_VERTEX_SHADER, POLYGON_FRAGMENT_SHADER_PREFIX + FogShader::buildBlock(fogSource) + POLYGON_FRAGMENT_SHADER_MAIN);

            // Get shader variables locations
            _a_color = _shader->getAttribLoc("a_color");
            _a_coord = _shader->getAttribLoc("a_coord");
            _u_mvpMat = _shader->getUniformLoc("u_mvpMat");
            _u_depthBias = _shader->getUniformLoc("u_depthBias");
            _u_depthBiasClip = _shader->getUniformLoc("u_depthBiasClip");
        }

        return _shader && _shader->isValid() && _lineRenderer.initializeRenderer();
    }
    
    void PolygonRenderer::bind(const ViewState &viewState) {
        // Prepare for drawing
        glUseProgram(_shader->getProgId());
        // This frame's fog, resolved once by the owner: markers, lines and overlays fade into the
        // same haze as the map they sit on.
        if (auto mapRenderer = _mapRenderer.lock()) {
            FogShader::setUniforms(_shader->getProgId(), mapRenderer->getFrameFog(), viewState);
        }
        // Colors, Coords
        glEnableVertexAttribArray(_a_color);
        glEnableVertexAttribArray(_a_coord);
        // Matrix
        const cglib::mat4x4<float>& mvpMat = viewState.getRTEModelviewProjectionMat();
        glUniformMatrix4fv(_u_mvpMat, 1, GL_FALSE, mvpMat.data());
        glUniform1f(_u_depthBias, _depthBias);
        glUniform1f(_u_depthBiasClip, _depthBiasClip);
    }
    
    void PolygonRenderer::unbind() {
        // Disable bound arrays
        glDisableVertexAttribArray(_a_color);
        glDisableVertexAttribArray(_a_coord);
    }
    
    bool PolygonRenderer::isEmptyBatch() const {
        return _drawDataBuffer.empty();
    }
    
    void PolygonRenderer::addToBatch(const std::shared_ptr<PolygonDrawData>& drawData, const ViewState& viewState) {
        const Bitmap* bitmap = drawData->getBitmap().get();
        
        if (_prevBitmap && _prevBitmap != bitmap) {
            drawBatch(viewState);
        }
        
        _drawDataBuffer.push_back(drawData);
        _prevBitmap = bitmap;
        
        if (!drawData->getLineDrawDatas().empty()) {
            if (_prevBitmap) {
                drawBatch(viewState);
            }

            unbind();
            
            // Draw the draw datas, multiple passes may be necessary
            for (const std::shared_ptr<LineDrawData>& lineDrawData : drawData->getLineDrawDatas()) {
                _lineRenderer.addToBatch(lineDrawData, viewState);
            }

            _lineRenderer.bind(viewState);
            _lineRenderer.drawBatch(viewState);
            _lineRenderer.unbind();

            bind(viewState);
        }
    }
    
    void PolygonRenderer::drawBatch(const ViewState& viewState) {
        if (_drawDataBuffer.empty()) {
            return;
        }

        // Build buffers and draw
        BuildAndDrawBuffers(_a_color, _a_coord, _colorBuf, _coordBuf, _indexBuf, _drawDataBuffer, viewState);
        
        _drawDataBuffer.clear();
        _prevBitmap = nullptr;
    }
    
    const std::string PolygonRenderer::POLYGON_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec4 a_coord;
        attribute vec4 a_color;
        varying vec4 v_color;
        uniform mat4 u_mvpMat;
        uniform float u_depthBias;
        uniform float u_depthBiasClip;
        void main() {
            v_color = a_color;
            vec4 clipPos = u_mvpMat * a_coord;
            clipPos.z -= u_depthBias * clipPos.w + u_depthBiasClip;
            gl_Position = clipPos;
        }
    )GLSL";

    const std::string PolygonRenderer::POLYGON_FRAGMENT_SHADER_PREFIX = R"GLSL(
        #version 100
        precision mediump float;
        varying lowp vec4 v_color;
)GLSL";

    const std::string PolygonRenderer::POLYGON_FRAGMENT_SHADER_MAIN = R"GLSL(
        void main() {
            vec4 color = v_color;
            if (color.a == 0.0) {
                discard;
            }
            gl_FragColor = applyFog(color);
        }
    )GLSL";
}
