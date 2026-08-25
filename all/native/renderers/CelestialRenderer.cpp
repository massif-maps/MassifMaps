#include "CelestialRenderer.h"
#include "celestial/CelestialArc.h"
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
            auto sprite = std::dynamic_pointer_cast<CelestialSprite>(object);
            if (!sprite || !sprite->isVisible()) {
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
            instance.halfSize = halfSize;
            instance.softness = sprite->getSoftness();
            instance.color[0] = static_cast<unsigned char>(color.getR() * opacity);
            instance.color[1] = static_cast<unsigned char>(color.getG() * opacity);
            instance.color[2] = static_cast<unsigned char>(color.getB() * opacity);
            instance.color[3] = static_cast<unsigned char>(color.getA() * opacity);
            instance.bitmap = sprite->getBitmap();
            instances.push_back(instance);
        }

        // Far to near, so overlapping discs blend in the order the eye expects.
        const cglib::vec3<double>& cameraPos = viewState.getCameraPos();
        std::sort(instances.begin(), instances.end(), [&cameraPos](const SpriteInstance& a, const SpriteInstance& b) {
            return cglib::norm(a.worldPos - cameraPos) > cglib::norm(b.worldPos - cameraPos);
        });
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
            _coordBuf.clear();
            _colorBuf.clear();
            _texCoordBuf.clear();
            _indexBuf.clear();

            std::size_t count = 0;
            while (index < instances.size() && instances[index].bitmap == batchBitmap && instances[index].softness == batchSoftness) {
                const SpriteInstance& instance = instances[index++];
                cglib::vec3<double> rel = instance.worldPos - cameraPos;
                static const float CORNERS[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                for (int i = 0; i < 4; i++) {
                    cglib::vec3<double> corner = rel + right * static_cast<double>(CORNERS[i][0] * instance.halfSize) + up * static_cast<double>(CORNERS[i][1] * instance.halfSize);
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
        const cglib::vec3<double>& cameraPos = viewState.getCameraPos();
        MapPos focusMapPos = projectionSurface->calculateMapPos(viewState.getFocusPos());
        double distance = viewState.getFar() * INFINITE_DISTANCE_FACTOR;

        bool bound = false;
        GLuint a_coord = 0;
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
            // A segmented arc is a set of separate lines, a plain one is a path through its points.
            std::size_t step = (arc->isSegmented() ? 2 : 1);

            _coordBuf.clear();
            _indexBuf.clear();
            unsigned short vertexCount = 0;
            for (std::size_t i = 0; i + 1 < directions.size(); i += step) {
                if (!belowHorizonVisible && (directions[i](2) < 0 || directions[i + 1](2) < 0)) {
                    continue;
                }
                for (int e = 0; e < 2; e++) {
                    const cglib::vec3<double>& dir = directions[i + e];
                    cglib::vec3<double> worldDir = projectionSurface->calculateVector(focusMapPos, MapVec(dir(0), dir(1), dir(2)));
                    cglib::vec3<double> rel = worldDir * distance;
                    _coordBuf.push_back(static_cast<float>(rel(0)));
                    _coordBuf.push_back(static_cast<float>(rel(1)));
                    _coordBuf.push_back(static_cast<float>(rel(2)));
                    _indexBuf.push_back(vertexCount++);
                }
            }
            if (_indexBuf.empty()) {
                continue;
            }

            if (!bound) {
                glUseProgram(_arcShader->getProgId());
                a_coord = _arcShader->getAttribLoc("a_coord");
                glUniformMatrix4fv(_arcShader->getUniformLoc("u_mvpMat"), 1, GL_FALSE, viewState.getRTEModelviewProjectionMat().data());
                setupFogUniforms(_arcShader->getProgId(), viewState);
                glEnableVertexAttribArray(a_coord);
                bound = true;
            }
            Color color = arc->getColor();
            glUniform4f(_arcShader->getUniformLoc("u_color"),
                        color.getR() / 255.0f * opacity, color.getG() / 255.0f * opacity,
                        color.getB() / 255.0f * opacity, color.getA() / 255.0f * opacity);
            glLineWidth(std::max(1.0f, arc->getWidth()));
            glVertexAttribPointer(a_coord, 3, GL_FLOAT, GL_FALSE, 0, _coordBuf.data());
            glDrawElements(GL_LINES, static_cast<GLsizei>(_indexBuf.size()), GL_UNSIGNED_SHORT, _indexBuf.data());
        }
        if (bound) {
            glDisableVertexAttribArray(a_coord);
            glLineWidth(1.0f);
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
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);

        std::vector<SpriteInstance> instances;
        buildSprites(viewState, opacity, instances);
        drawSprites(instances, viewState);
        drawArcs(viewState, opacity);

        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        return false;
    }

    void CelestialRenderer::calculateRayIntersectedElements(const std::shared_ptr<CelestialLayer>& layer, const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        cglib::vec3<double> rayDir = cglib::unit(ray.direction);
        calculateRayIntersectedArcs(layer, ray, rayDir, viewState, results);
        for (const std::shared_ptr<CelestialObject>& object : _objects) {
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
                // Curves are all parked at the same distance, so the click handler - which orders
                // by distance from the camera - would pick between two overlapping ones by list
                // order. Reporting the hit a hair further away the wider it was missed makes the
                // curve the touch actually aimed at win, and leaves sprites (reported at their
                // true distance) ahead of a curve running through them.
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
    // sun dims into the band rather than staying crisp over a hazed horizon. These are drawn with
    // straight alpha, not premultiplied, so the colour goes through the premultiplied contract and
    // comes back out.
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
                color *= texture2D(u_tex, v_texCoord);
            } else {
                // No bitmap: a disc, soft at the edge by u_softness. Cheaper than a texture and
                // enough for a disc or a point of light.
                float d = length(v_texCoord - vec2(0.5)) * 2.0;
                color.a *= 1.0 - smoothstep(1.0 - u_softness, 1.0, d);
            }
            gl_FragColor = fogCelestial(color);
        }
    )GLSL";

    const std::string CelestialRenderer::ARC_VERTEX_SHADER = R"GLSL(
        attribute vec3 a_coord;
        uniform mat4 u_mvpMat;
        void main() {
            gl_Position = u_mvpMat * vec4(a_coord, 1.0);
        }
    )GLSL";

    const std::string CelestialRenderer::ARC_FRAGMENT_SHADER_PREFIX = R"GLSL(
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        uniform vec4 u_color;
    )GLSL";

    const std::string CelestialRenderer::ARC_FRAGMENT_SHADER_MAIN = R"GLSL(
        void main() {
            gl_FragColor = fogCelestial(u_color);
        }
    )GLSL";

}
