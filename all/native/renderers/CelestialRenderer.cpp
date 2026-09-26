#include "CelestialRenderer.h"
#include "celestial/CelestialArc.h"
#include "celestial/CelestialLabel.h"
#include "celestial/CelestialObject.h"
#include "celestial/CelestialSprite.h"
#include "components/Options.h"
#include "graphics/Bitmap.h"
#include "graphics/ViewState.h"
#include "layers/CelestialLayer.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "renderers/MapRenderer.h"
#include "renderers/components/RayIntersectedElement.h"
#include "renderers/utils/FogShader.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/Shader.h"
#include "renderers/utils/Texture.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>

namespace massif {

    const double CelestialRenderer::INFINITE_DISTANCE_FACTOR = 0.9;

    CelestialRenderer::CelestialRenderer() :
        _spriteShader(),
        _arcShader(),
        _options(),
        _mapRenderer(),
        _objects(),
        _coordBuf(),
        _colorBuf(),
        _texCoordBuf(),
        _indexBuf(),
        _mutex()
    {
    }

    CelestialRenderer::~CelestialRenderer() {
    }

    void CelestialRenderer::setComponents(const std::weak_ptr<Options>& options, const std::weak_ptr<MapRenderer>& mapRenderer) {
        std::lock_guard<std::mutex> lock(_mutex);
        _options = options;
        _mapRenderer = mapRenderer;
        _spriteShader.reset();
        _arcShader.reset();
    }

    void CelestialRenderer::refreshObjects(const std::vector<std::shared_ptr<CelestialObject> >& objects) {
        std::lock_guard<std::mutex> lock(_mutex);
        _objects = objects;
    }

    bool CelestialRenderer::initializeRenderer() {
        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return false;
        }
        // The custom fog shader is compiled into both programs, so a change to it has to rebuild.
        std::string fogSource = FogShader::source(mapRenderer->getOptions());
        if (_spriteShader && _arcShader && _fogShaderSource == fogSource) {
            return true;
        }
        std::shared_ptr<GLResourceManager> resourceManager = mapRenderer->getGLResourceManager();
        if (!resourceManager) {
            return false;
        }
        _fogShaderSource = fogSource;
        std::string fogBlock = FogShader::buildBlock(fogSource);
        _spriteShader = resourceManager->create<Shader>("celestial_sprite", SPRITE_VERTEX_SHADER, SPRITE_FRAGMENT_SHADER_PREFIX + fogBlock + CELESTIAL_FRAGMENT_SHADER_FOG + SPRITE_FRAGMENT_SHADER_MAIN);
        _arcShader = resourceManager->create<Shader>("celestial_arc", ARC_VERTEX_SHADER, ARC_FRAGMENT_SHADER_PREFIX + fogBlock + CELESTIAL_FRAGMENT_SHADER_FOG + ARC_FRAGMENT_SHADER_MAIN);
        return static_cast<bool>(_spriteShader) && static_cast<bool>(_arcShader);
    }

    void CelestialRenderer::setupFogUniforms(GLuint progId, const ViewState& viewState) const {
        if (std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock()) {
            FogShader::setUniforms(progId, mapRenderer->getFrameFog(), viewState);
        }
    }

    bool CelestialRenderer::resolveWorldPos(const std::shared_ptr<CelestialObject>& object, const ViewState& viewState, cglib::vec3<double>& worldPos, double& distance) const {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return false;
        }
        const cglib::vec3<double>& cameraPos = viewState.getCameraPos();

        if (object->isDirectionAnchored()) {
            // The direction is given in the local frame at the camera - x east, y north, z up -
            // and the projection surface is what turns that into a world vector, so this works on
            // a sphere as well as on a plane.
            MapPos focusMapPos = projectionSurface->calculateMapPos(viewState.getFocusPos());
            cglib::vec3<double> dir = object->calculateDirectionVector();
            cglib::vec3<double> worldDir = projectionSurface->calculateVector(focusMapPos, MapVec(dir(0), dir(1), dir(2)));
            if (cglib::norm(worldDir) < 1.0e-12) {
                return false;
            }
            worldDir = cglib::unit(worldDir);

            double objectDistance = object->getDistance();
            if (objectDistance <= 0) {
                // Infinitely far: park it just inside the far plane. Everything the map draws is
                // nearer, so the map covers it, and it never moves when the camera pans.
                distance = viewState.getFar() * INFINITE_DISTANCE_FACTOR;
            } else {
                distance = objectDistance * Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE;
                distance = std::min(distance, static_cast<double>(viewState.getFar()) * INFINITE_DISTANCE_FACTOR);
            }
            worldPos = cameraPos + worldDir * distance;
            return true;
        }

        std::shared_ptr<Options> options = _options.lock();
        if (!options) {
            return false;
        }
        MapPos internalPos = options->getBaseProjection()->toInternal(object->getPosition());
        cglib::vec3<double> surfacePos = projectionSurface->calculatePosition(internalPos);
        cglib::vec3<double> normal = projectionSurface->calculateNormal(internalPos);
        double altitude = object->getPositionAltitude() * Const::WORLD_SIZE / Const::EARTH_CIRCUMFERENCE;
        worldPos = surfacePos + normal * altitude;
        distance = cglib::length(worldPos - cameraPos);
        return distance > 0;
    }

    void CelestialRenderer::buildSprites(const ViewState& viewState, float opacity, std::vector<SpriteInstance>& instances) const {
        double tanHalfFovY = std::tan(viewState.getFOVY() * 0.5 * Const::DEG_TO_RAD);
        float halfHeight = std::max(1.0f, viewState.getHalfHeight());

        for (const std::shared_ptr<CelestialObject>& object : _objects) {
            if (!object->isVisible()) {
                continue;
            }
            if (auto label = std::dynamic_pointer_cast<CelestialLabel>(object)) {
                cglib::vec3<double> worldPos;
                double distance = 0;
                SpriteInstance instance;
                if (resolveWorldPos(object, viewState, worldPos, distance) && buildLabel(label, viewState, distance, instance)) {
                    instance.worldPos = worldPos;
                    Color color = label->getColor();
                    instance.color[0] = static_cast<unsigned char>(color.getR() * opacity);
                    instance.color[1] = static_cast<unsigned char>(color.getG() * opacity);
                    instance.color[2] = static_cast<unsigned char>(color.getB() * opacity);
                    instance.color[3] = static_cast<unsigned char>(color.getA() * opacity);
                    instances.push_back(instance);
                }
                continue;
            }
            auto sprite = std::dynamic_pointer_cast<CelestialSprite>(object);
            if (!sprite) {
                continue;
            }
            cglib::vec3<double> worldPos;
            double distance = 0;
            if (!resolveWorldPos(object, viewState, worldPos, distance)) {
                continue;
            }

            float halfSize = 0;
            if (sprite->getAngularSize() > 0) {
                // A real body: its size is an angle, so what it covers in world units grows with
                // the distance it was placed at, and it ends up the same angular size either way.
                halfSize = static_cast<float>(distance * std::tan(sprite->getAngularSize() * 0.5 * Const::DEG_TO_RAD));
            } else {
                // A fixed number of pixels: one pixel is this many world units at that distance.
                double worldPerPixel = distance * tanHalfFovY / halfHeight;
                halfSize = static_cast<float>(sprite->getScreenSize() * 0.5 * worldPerPixel);
            }
            if (!(halfSize > 0)) {
                continue;
            }

            Color color = sprite->getColor();
            SpriteInstance instance;
            instance.object = object;
            instance.worldPos = worldPos;
            instance.halfWidth = halfSize;
            instance.halfHeight = halfSize;
            instance.shiftRight = 0;
            instance.shiftUp = 0;
            instance.softness = sprite->getSoftness();
            instance.occluded = sprite->isOccludedByMap();
            instance.color[0] = static_cast<unsigned char>(color.getR() * opacity);
            instance.color[1] = static_cast<unsigned char>(color.getG() * opacity);
            instance.color[2] = static_cast<unsigned char>(color.getB() * opacity);
            instance.color[3] = static_cast<unsigned char>(color.getA() * opacity);
            instance.bitmap = sprite->getBitmap();
            instances.push_back(instance);
        }

        // Far to near, so overlapping discs blend in the order the eye expects. Stable: sky objects
        // are all equally far, and then the layer's own order decides.
        const cglib::vec3<double>& cameraPos = viewState.getCameraPos();
        std::stable_sort(instances.begin(), instances.end(), [&cameraPos](const SpriteInstance& a, const SpriteInstance& b) {
            return cglib::norm(a.worldPos - cameraPos) > cglib::norm(b.worldPos - cameraPos);
        });
    }

    bool CelestialRenderer::buildLabel(const std::shared_ptr<CelestialLabel>& label, const ViewState& viewState, double distance, SpriteInstance& instance) const {
        std::shared_ptr<Bitmap> bitmap = label->buildBitmap(viewState.getDPToPX());
        if (!bitmap) {
            return false;
        }
        // Drawn at the bitmap's own pixel size: one pixel is this many world units at that distance.
        double tanHalfFovY = std::tan(viewState.getFOVY() * 0.5 * Const::DEG_TO_RAD);
        double worldPerPixel = distance * tanHalfFovY / std::max(1.0f, viewState.getHalfHeight());
        float dpToPx = viewState.getDPToPX();
        instance.object = label;
        instance.halfWidth = static_cast<float>(bitmap->getWidth() * 0.5 * worldPerPixel);
        instance.halfHeight = static_cast<float>(bitmap->getHeight() * 0.5 * worldPerPixel);
        // The anchor point goes on the direction, then the offset moves the whole label on screen.
        instance.shiftRight = static_cast<float>(-label->getAnchorPointX() * instance.halfWidth + label->getOffsetX() * dpToPx * worldPerPixel);
        instance.shiftUp = static_cast<float>(-label->getAnchorPointY() * instance.halfHeight + label->getOffsetY() * dpToPx * worldPerPixel);
        instance.softness = 0;
        instance.occluded = label->isOccludedByMap();
        instance.bitmap = bitmap;
        return true;
    }

    void CelestialRenderer::drawSprites(const std::vector<SpriteInstance>& instances, const ViewState& viewState) {
        if (instances.empty()) {
            return;
        }
        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return;
        }

        // Camera-facing basis, taken from the view matrix rows: everything is a billboard here.
        const cglib::mat4x4<double>& mvMat = viewState.getModelviewMat();
        cglib::vec3<double> right(mvMat(0, 0), mvMat(0, 1), mvMat(0, 2));
        cglib::vec3<double> up(mvMat(1, 0), mvMat(1, 1), mvMat(1, 2));
        const cglib::vec3<double>& cameraPos = viewState.getCameraPos();

        glUseProgram(_spriteShader->getProgId());
        GLuint a_coord = _spriteShader->getAttribLoc("a_coord");
        GLuint a_texCoord = _spriteShader->getAttribLoc("a_texCoord");
        GLuint a_color = _spriteShader->getAttribLoc("a_color");
        glUniformMatrix4fv(_spriteShader->getUniformLoc("u_mvpMat"), 1, GL_FALSE, viewState.getRTEModelviewProjectionMat().data());
        setupFogUniforms(_spriteShader->getProgId(), viewState);
        glUniform1i(_spriteShader->getUniformLoc("u_tex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glEnableVertexAttribArray(a_coord);
        glEnableVertexAttribArray(a_texCoord);
        glEnableVertexAttribArray(a_color);

        // One batch per bitmap: a catalogue of thousands that shares a bitmap, or none at all, is
        // a single draw call.
        std::size_t index = 0;
        while (index < instances.size()) {
            const std::shared_ptr<Bitmap>& batchBitmap = instances[index].bitmap;
            float batchSoftness = instances[index].softness;
            bool batchOccluded = instances[index].occluded;
            _coordBuf.clear();
            _colorBuf.clear();
            _texCoordBuf.clear();
            _indexBuf.clear();

            std::size_t count = 0;
            while (index < instances.size() && instances[index].bitmap == batchBitmap && instances[index].softness == batchSoftness && instances[index].occluded == batchOccluded) {
                const SpriteInstance& instance = instances[index++];
                cglib::vec3<double> rel = instance.worldPos - cameraPos;
                static const float CORNERS[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                for (int i = 0; i < 4; i++) {
                    cglib::vec3<double> corner = rel + right * static_cast<double>(instance.shiftRight + CORNERS[i][0] * instance.halfWidth) + up * static_cast<double>(instance.shiftUp + CORNERS[i][1] * instance.halfHeight);
                    _coordBuf.push_back(static_cast<float>(corner(0)));
                    _coordBuf.push_back(static_cast<float>(corner(1)));
                    _coordBuf.push_back(static_cast<float>(corner(2)));
                    _texCoordBuf.push_back(CORNERS[i][0] * 0.5f + 0.5f);
                    _texCoordBuf.push_back(CORNERS[i][1] * 0.5f + 0.5f);
                    for (int c = 0; c < 4; c++) {
                        _colorBuf.push_back(instance.color[c]);
                    }
                }
                unsigned short base = static_cast<unsigned short>(count * 4);
                _indexBuf.push_back(base + 0);
                _indexBuf.push_back(base + 1);
                _indexBuf.push_back(base + 2);
                _indexBuf.push_back(base + 0);
                _indexBuf.push_back(base + 2);
                _indexBuf.push_back(base + 3);
                count++;
            }

            std::shared_ptr<Texture> texture;
            if (batchBitmap) {
                // Null once the surface is gone - a frame still in flight has nothing to create
                // into (#178). The batch then draws untextured rather than crashing.
                if (std::shared_ptr<GLResourceManager> glResourceManager = mapRenderer->getGLResourceManager()) {
                    texture = glResourceManager->create<Texture>(batchBitmap, false, false);
                }
            }
            glUniform1f(_spriteShader->getUniformLoc("u_hasTex"), texture ? 1.0f : 0.0f);
            glUniform1f(_spriteShader->getUniformLoc("u_softness"), std::max(0.001f, batchSoftness));
            if (texture) {
                glBindTexture(GL_TEXTURE_2D, texture->getTexId());
            }

            if (batchOccluded) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            glVertexAttribPointer(a_coord, 3, GL_FLOAT, GL_FALSE, 0, _coordBuf.data());
            glVertexAttribPointer(a_texCoord, 2, GL_FLOAT, GL_FALSE, 0, _texCoordBuf.data());
            glVertexAttribPointer(a_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, _colorBuf.data());
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_indexBuf.size()), GL_UNSIGNED_SHORT, _indexBuf.data());
        }

        glDisableVertexAttribArray(a_coord);
        glDisableVertexAttribArray(a_texCoord);
        glDisableVertexAttribArray(a_color);
    }

    void CelestialRenderer::drawArcs(const ViewState& viewState, float opacity) {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return;
        }
        MapPos focusMapPos = projectionSurface->calculateMapPos(viewState.getFocusPos());
        double distance = viewState.getFar() * INFINITE_DISTANCE_FACTOR;
        // A point behind the eye has no place on screen to widen a line from, so a run is cut
        // there. Nothing that near the side of the view is on screen for any usable field of view.
        const cglib::mat4x4<double>& mvMat = viewState.getModelviewMat();
        cglib::vec3<double> forward(-mvMat(2, 0), -mvMat(2, 1), -mvMat(2, 2));

        bool bound = false;
        GLuint a_coord = 0;
        GLuint a_prev = 0;
        GLuint a_next = 0;
        GLuint a_side = 0;
        for (const std::shared_ptr<CelestialObject>& object : _objects) {
            auto arc = std::dynamic_pointer_cast<CelestialArc>(object);
            if (!arc || !arc->isVisible()) {
                continue;
            }
            std::vector<cglib::vec3<double> > directions = arc->buildDirections();
            if (directions.size() < 2) {
                continue;
            }
            bool belowHorizonVisible = arc->isBelowHorizonVisible();
            bool segmented = arc->isSegmented();

            std::vector<cglib::vec3<float> > points;
            std::vector<bool> usable;
            points.reserve(directions.size());
            usable.reserve(directions.size());
            for (const cglib::vec3<double>& dir : directions) {
                cglib::vec3<double> worldDir = cglib::unit(projectionSurface->calculateVector(focusMapPos, MapVec(dir(0), dir(1), dir(2))));
                points.push_back(cglib::vec3<float>::convert(worldDir * distance));
                usable.push_back((belowHorizonVisible || dir(2) >= 0) && cglib::dot_product(worldDir, forward) > 0.02);
            }

            // A strip of quads widened on screen by the vertex shader, so the width is in pixels at
            // any field of view: glLineWidth is capped at 1 on WebGL and most GLES drivers.
            _coordBuf.clear();
            _texCoordBuf.clear();
            _indexBuf.clear();
            std::vector<float> prevBuf;
            std::vector<float> nextBuf;
            auto pushRun = [&](std::size_t first, std::size_t last) {
                if (last <= first || _coordBuf.size() / 3 + (last - first + 1) * 2 > 65535) {
                    return;
                }
                unsigned short base = static_cast<unsigned short>(_coordBuf.size() / 3);
                for (std::size_t i = first; i <= last; i++) {
                    const cglib::vec3<float>& prev = points[i > first ? i - 1 : i];
                    const cglib::vec3<float>& next = points[i < last ? i + 1 : i];
                    for (int side = -1; side <= 1; side += 2) {
                        for (int c = 0; c < 3; c++) {
                            _coordBuf.push_back(points[i](c));
                            prevBuf.push_back(prev(c));
                            nextBuf.push_back(next(c));
                        }
                        _texCoordBuf.push_back(static_cast<float>(side));
                    }
                }
                for (std::size_t i = 0; i < last - first; i++) {
                    unsigned short v = static_cast<unsigned short>(base + i * 2);
                    _indexBuf.push_back(v);
                    _indexBuf.push_back(v + 1);
                    _indexBuf.push_back(v + 2);
                    _indexBuf.push_back(v + 1);
                    _indexBuf.push_back(v + 3);
                    _indexBuf.push_back(v + 2);
                }
            };
            if (segmented) {
                for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
                    if (usable[i] && usable[i + 1]) {
                        pushRun(i, i + 1);
                    }
                }
            } else {
                std::size_t i = 0;
                while (i < points.size()) {
                    if (!usable[i]) {
                        i++;
                        continue;
                    }
                    std::size_t first = i;
                    while (i + 1 < points.size() && usable[i + 1]) {
                        i++;
                    }
                    pushRun(first, i);
                    i++;
                }
            }
            if (_indexBuf.empty()) {
                continue;
            }

            if (!bound) {
                glUseProgram(_arcShader->getProgId());
                a_coord = _arcShader->getAttribLoc("a_coord");
                a_prev = _arcShader->getAttribLoc("a_prev");
                a_next = _arcShader->getAttribLoc("a_next");
                a_side = _arcShader->getAttribLoc("a_side");
                glUniformMatrix4fv(_arcShader->getUniformLoc("u_mvpMat"), 1, GL_FALSE, viewState.getRTEModelviewProjectionMat().data());
                glUniform2f(_arcShader->getUniformLoc("u_halfViewport"), viewState.getHalfWidth(), viewState.getHalfHeight());
                setupFogUniforms(_arcShader->getProgId(), viewState);
                glEnableVertexAttribArray(a_coord);
                glEnableVertexAttribArray(a_prev);
                glEnableVertexAttribArray(a_next);
                glEnableVertexAttribArray(a_side);
                bound = true;
            }
            Color color = arc->getColor();
            glUniform4f(_arcShader->getUniformLoc("u_color"),
                        color.getR() / 255.0f * opacity, color.getG() / 255.0f * opacity,
                        color.getB() / 255.0f * opacity, color.getA() / 255.0f * opacity);
            // Half a pixel more each side, faded out in the fragment shader: smooth without multisampling.
            if (arc->isOccludedByMap()) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            glUniform1f(_arcShader->getUniformLoc("u_halfWidth"), std::max(0.5f, arc->getWidth() * 0.5f) + 0.5f);
            glVertexAttribPointer(a_coord, 3, GL_FLOAT, GL_FALSE, 0, _coordBuf.data());
            glVertexAttribPointer(a_prev, 3, GL_FLOAT, GL_FALSE, 0, prevBuf.data());
            glVertexAttribPointer(a_next, 3, GL_FLOAT, GL_FALSE, 0, nextBuf.data());
            glVertexAttribPointer(a_side, 1, GL_FLOAT, GL_FALSE, 0, _texCoordBuf.data());
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_indexBuf.size()), GL_UNSIGNED_SHORT, _indexBuf.data());
        }
        if (bound) {
            glDisableVertexAttribArray(a_coord);
            glDisableVertexAttribArray(a_prev);
            glDisableVertexAttribArray(a_next);
            glDisableVertexAttribArray(a_side);
        }
    }

    bool CelestialRenderer::onDrawFrame(float deltaSeconds, float opacity, const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_objects.empty()) {
            return false;
        }
        if (!initializeRenderer()) {
            return false;
        }

        // Depth-tested but not depth-writing: the map in front covers a sky object, and the object
        // never occludes anything itself.
        glEnable(GL_DEPTH_TEST);
        // LEQUAL, not whatever the layer before left: the tile layers end on LESS, and an object parked
        // at the far plane then lost to the cleared depth of the empty sky around it.
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);

        // Curves first: a body or a label on a curve reads on top of it.
        drawArcs(viewState, opacity);
        std::vector<SpriteInstance> instances;
        buildSprites(viewState, opacity, instances);
        drawSprites(instances, viewState);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        return false;
    }

    void CelestialRenderer::calculateRayIntersectedElements(const std::shared_ptr<CelestialLayer>& layer, const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        cglib::vec3<double> rayDir = cglib::unit(ray.direction);
        calculateRayIntersectedArcs(layer, ray, rayDir, viewState, results);
        const cglib::mat4x4<double>& mvMat = viewState.getModelviewMat();
        cglib::vec3<double> right(mvMat(0, 0), mvMat(0, 1), mvMat(0, 2));
        cglib::vec3<double> up(mvMat(1, 0), mvMat(1, 1), mvMat(1, 2));
        for (const std::shared_ptr<CelestialObject>& object : _objects) {
            auto label = std::dynamic_pointer_cast<CelestialLabel>(object);
            if (label) {
                cglib::vec3<double> worldPos;
                double distance = 0;
                SpriteInstance instance;
                if (!label->isVisible() || !label->isClickable() || !resolveWorldPos(object, viewState, worldPos, distance) || !buildLabel(label, viewState, distance, instance)) {
                    continue;
                }
                // The label's rectangle, on the plane through its centre facing the camera.
                cglib::vec3<double> centre = worldPos + right * static_cast<double>(instance.shiftRight) + up * static_cast<double>(instance.shiftUp);
                cglib::vec3<double> toCentre = centre - ray.origin;
                double centreDistance = cglib::length(toCentre);
                if (!(centreDistance > 0)) {
                    continue;
                }
                double along = cglib::dot_product(rayDir, toCentre / centreDistance);
                if (!(along > 0)) {
                    continue;
                }
                cglib::vec3<double> onPlane = rayDir * (centreDistance / along) - toCentre;
                if (std::abs(cglib::dot_product(onPlane, right)) > instance.halfWidth || std::abs(cglib::dot_product(onPlane, up)) > instance.halfHeight) {
                    continue;
                }
                results.push_back(RayIntersectedElement(std::static_pointer_cast<CelestialObject>(object), layer, ray.origin + rayDir * (centreDistance / along), worldPos, true));
                continue;
            }
            auto sprite = std::dynamic_pointer_cast<CelestialSprite>(object);
            if (!sprite || !sprite->isVisible()) {
                continue;
            }
            cglib::vec3<double> worldPos;
            double distance = 0;
            if (!resolveWorldPos(object, viewState, worldPos, distance)) {
                continue;
            }
            cglib::vec3<double> toObject = worldPos - ray.origin;
            double objectDistance = cglib::length(toObject);
            if (objectDistance <= 0) {
                continue;
            }
            // Angular test, which is the natural one here: how far off the touch ray is from the
            // direction the object sits in. A sprite a pixel across gets the click radius its own
            // setting asks for, or nobody could ever hit it.
            double cosAngle = cglib::dot_product(cglib::unit(toObject), rayDir);
            if (cosAngle <= 0) {
                continue;
            }
            double angle = std::acos(std::min(1.0, cosAngle));
            double radius = std::atan2(static_cast<double>(sprite->getScreenSize() > 0 ? 0.0f : sprite->getAngularSize()) * 0.5 * Const::DEG_TO_RAD, 1.0);
            radius = std::max(radius, sprite->getClickRadius() * Const::DEG_TO_RAD);
            if (sprite->getScreenSize() > 0) {
                // A pixel-sized sprite: convert its half size on screen into an angle.
                double tanHalfFovY = std::tan(viewState.getFOVY() * 0.5 * Const::DEG_TO_RAD);
                double halfHeight = std::max(1.0f, viewState.getHalfHeight());
                radius = std::max(radius, std::atan(sprite->getScreenSize() * 0.5 * tanHalfFovY / halfHeight));
            }
            if (angle > radius) {
                continue;
            }
            cglib::vec3<double> hitPos = ray.origin + rayDir * objectDistance;
            results.push_back(RayIntersectedElement(std::static_pointer_cast<CelestialObject>(object), layer, hitPos, worldPos, true));
        }
    }

    void CelestialRenderer::calculateRayIntersectedArcs(const std::shared_ptr<CelestialLayer>& layer, const cglib::ray3<double>& ray, const cglib::vec3<double>& rayDir, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
        std::shared_ptr<ProjectionSurface> projectionSurface = viewState.getProjectionSurface();
        if (!projectionSurface) {
            return;
        }
        MapPos focusMapPos = projectionSurface->calculateMapPos(viewState.getFocusPos());
        double distance = viewState.getFar() * INFINITE_DISTANCE_FACTOR;

        for (const std::shared_ptr<CelestialObject>& object : _objects) {
            auto arc = std::dynamic_pointer_cast<CelestialArc>(object);
            if (!arc || !arc->isVisible() || !(arc->getClickRadius() > 0)) {
                continue;
            }
            std::vector<cglib::vec3<double> > directions = arc->buildDirections();
            if (directions.size() < 2) {
                continue;
            }
            bool belowHorizonVisible = arc->isBelowHorizonVisible();
            double radius = arc->getClickRadius() * Const::DEG_TO_RAD;
            double bestCos = std::cos(radius);
            bool hit = false;
            std::size_t step = (arc->isSegmented() ? 2 : 1);

            // The same angular test the sprites use, against the nearest point of the curve: for
            // every segment, the closest point of the chord to the ray direction, brought back onto
            // the unit sphere. A curve drawn two pixels wide is otherwise unhittable.
            for (std::size_t i = 0; i + 1 < directions.size(); i += step) {
                if (!belowHorizonVisible && (directions[i](2) < 0 || directions[i + 1](2) < 0)) {
                    continue;
                }
                cglib::vec3<double> u = cglib::unit(projectionSurface->calculateVector(focusMapPos, MapVec(directions[i](0), directions[i](1), directions[i](2))));
                cglib::vec3<double> v = cglib::unit(projectionSurface->calculateVector(focusMapPos, MapVec(directions[i + 1](0), directions[i + 1](1), directions[i + 1](2))));
                cglib::vec3<double> edge = v - u;
                double edgeNorm = cglib::norm(edge);
                double t = (edgeNorm > 0 ? cglib::dot_product(rayDir - u, edge) / edgeNorm : 0.0);
                t = std::max(0.0, std::min(1.0, t));
                cglib::vec3<double> closest = u + edge * t;
                if (cglib::norm(closest) <= 0) {
                    continue;
                }
                double cosAngle = cglib::dot_product(cglib::unit(closest), rayDir);
                if (cosAngle > bestCos) {
                    bestCos = cosAngle;
                    hit = true;
                }
            }
            if (hit) {
                // Curves are all parked at the same distance, so the click handler would pick
                // between two overlapping ones by list order. Reporting the hit a hair further away
                // the wider it was missed makes the curve the touch aimed at win.
                cglib::vec3<double> hitPos = ray.origin + rayDir * (distance / bestCos);
                results.push_back(RayIntersectedElement(std::static_pointer_cast<CelestialObject>(object), layer, hitPos, hitPos, true));
            }
        }
    }

    const std::string CelestialRenderer::SPRITE_VERTEX_SHADER = R"GLSL(
        attribute vec3 a_coord;
        attribute vec2 a_texCoord;
        attribute vec4 a_color;
        uniform mat4 u_mvpMat;
        varying vec2 v_texCoord;
        varying vec4 v_color;
        void main() {
            v_texCoord = a_texCoord;
            v_color = a_color;
            gl_Position = u_mvpMat * vec4(a_coord, 1.0);
        }
    )GLSL";

    const std::string CelestialRenderer::SPRITE_FRAGMENT_SHADER_PREFIX = R"GLSL(
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        uniform sampler2D u_tex;
        uniform float u_hasTex;
        uniform float u_softness;
        varying vec2 v_texCoord;
        varying vec4 v_color;
    )GLSL";

    // A sky object is at infinity like the sky behind it, so it takes the ANGULAR haze - a setting
    // sun dims into the band rather than staying crisp. Drawn with straight alpha, so the colour
    // goes through the premultiplied contract and comes back out.
    const std::string CelestialRenderer::CELESTIAL_FRAGMENT_SHADER_FOG = R"GLSL(
        vec4 fogCelestial(vec4 color) {
            vec4 premul = skyFog(vec4(color.rgb * color.a, color.a), normalize(fogRayVec()));
            return vec4(premul.a > 0.0 ? premul.rgb / premul.a : premul.rgb, premul.a);
        }
    )GLSL";

    const std::string CelestialRenderer::SPRITE_FRAGMENT_SHADER_MAIN = R"GLSL(
        void main() {
            vec4 color = v_color;
            if (u_hasTex > 0.5) {
                // A Bitmap is premultiplied; the colour here is straight until fogCelestial.
                vec4 texel = texture2D(u_tex, v_texCoord);
                color *= vec4(texel.a > 0.0 ? texel.rgb / texel.a : texel.rgb, texel.a);
            } else {
                // No bitmap: a disc, soft at the edge by u_softness. Cheaper than a texture and
                // enough for a disc or a point of light.
                float d = length(v_texCoord - vec2(0.5)) * 2.0;
                color.a *= 1.0 - smoothstep(1.0 - u_softness, 1.0, d);
            }
            gl_FragColor = fogCelestial(color);
        }
    )GLSL";

    // The line is widened here, on screen: every vertex is pushed sideways by the half width, along
    // the normal of the two segments that meet at it (a miter, capped so a sharp turn cannot spike).
    const std::string CelestialRenderer::ARC_VERTEX_SHADER = R"GLSL(
        attribute vec3 a_coord;
        attribute vec3 a_prev;
        attribute vec3 a_next;
        attribute float a_side;
        uniform mat4 u_mvpMat;
        uniform vec2 u_halfViewport;
        uniform float u_halfWidth;
        varying float v_side;
        varying float v_halfWidth;
        vec2 toScreen(vec4 clip) {
            return clip.xy / clip.w * u_halfViewport;
        }
        void main() {
            vec4 clip = u_mvpMat * vec4(a_coord, 1.0);
            vec2 here = toScreen(clip);
            vec2 before = here - toScreen(u_mvpMat * vec4(a_prev, 1.0));
            vec2 after = toScreen(u_mvpMat * vec4(a_next, 1.0)) - here;
            if (dot(before, before) < 1.0e-6) { before = after; }
            if (dot(after, after) < 1.0e-6) { after = before; }
            vec2 inDir = normalize(before + vec2(1.0e-6, 0.0));
            vec2 outDir = normalize(after + vec2(1.0e-6, 0.0));
            vec2 tangent = normalize(inDir + outDir + vec2(0.0, 1.0e-6));
            vec2 normal = vec2(-tangent.y, tangent.x);
            float miter = 1.0 / max(0.5, dot(normal, vec2(-outDir.y, outDir.x)));
            vec2 offset = normal * a_side * u_halfWidth * miter;
            v_side = a_side;
            v_halfWidth = u_halfWidth;
            gl_Position = vec4(clip.xy + offset / u_halfViewport * clip.w, clip.zw);
        }
    )GLSL";

    const std::string CelestialRenderer::ARC_FRAGMENT_SHADER_PREFIX = R"GLSL(
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        uniform vec4 u_color;
        varying float v_side;
        varying float v_halfWidth;
    )GLSL";

    const std::string CelestialRenderer::ARC_FRAGMENT_SHADER_MAIN = R"GLSL(
        void main() {
            vec4 color = u_color;
            color.a *= clamp((1.0 - abs(v_side)) * v_halfWidth, 0.0, 1.0);
            gl_FragColor = fogCelestial(color);
        }
    )GLSL";

}
