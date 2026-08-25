/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALRENDERER_H_
#define _MASSIF_CELESTIALRENDERER_H_

#include "renderers/utils/GLContext.h"

#include <memory>
#include <mutex>
#include <vector>

#include <cglib/vec.h>
#include <cglib/ray.h>

namespace massif {
    class Bitmap;
    class CelestialLayer;
    class CelestialObject;
    class GLResourceManager;
    class MapRenderer;
    class Options;
    class RayIntersectedElement;
    class Shader;
    class Texture;
    class TextureManager;
    class ViewState;

    /**
     * Draws the objects of a CelestialLayer.
     *
     * Sprites are billboards expanded in the vertex shader and batched by bitmap, so any number of
     * objects sharing one bitmap - or none, the plain disc case - is a single draw call. Arcs are
     * line strips built once per change and drawn together.
     *
     * Depth: objects are drawn depth-TESTED but do not write depth. A direction-anchored object is
     * placed just inside the far plane, so the map and the terrain in front of it cover it exactly
     * as they should, while it never occludes anything itself.
     */
    class CelestialRenderer {
    public:
        CelestialRenderer();
        virtual ~CelestialRenderer();

        void setComponents(const std::weak_ptr<Options>& options, const std::weak_ptr<MapRenderer>& mapRenderer);

        void refreshObjects(const std::vector<std::shared_ptr<CelestialObject> >& objects);

        bool onDrawFrame(float deltaSeconds, float opacity, const ViewState& viewState);

        void calculateRayIntersectedElements(const std::shared_ptr<CelestialLayer>& layer, const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const;

    private:
        // A sprite ready to draw: its world position for this frame, and the size that position
        // implies in world units.
        struct SpriteInstance {
            std::shared_ptr<CelestialObject> object;
            cglib::vec3<double> worldPos;
            float halfSize;                 // world units at worldPos
            float softness;
            unsigned char color[4];
            std::shared_ptr<Bitmap> bitmap;
        };

        bool initializeRenderer();
        void setupFogUniforms(GLuint progId, const ViewState& viewState) const;
        bool resolveWorldPos(const std::shared_ptr<CelestialObject>& object, const ViewState& viewState, cglib::vec3<double>& worldPos, double& distance) const;
        void buildSprites(const ViewState& viewState, float opacity, std::vector<SpriteInstance>& instances) const;
        void drawSprites(const std::vector<SpriteInstance>& instances, const ViewState& viewState);
        void drawArcs(const ViewState& viewState, float opacity);
        void calculateRayIntersectedArcs(const std::shared_ptr<CelestialLayer>& layer, const cglib::ray3<double>& ray, const cglib::vec3<double>& rayDir, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const;

        static const std::string SPRITE_VERTEX_SHADER;
        static const std::string SPRITE_FRAGMENT_SHADER_PREFIX;
        static const std::string SPRITE_FRAGMENT_SHADER_MAIN;
        static const std::string CELESTIAL_FRAGMENT_SHADER_FOG;
        static const std::string ARC_VERTEX_SHADER;
        static const std::string ARC_FRAGMENT_SHADER_PREFIX;
        static const std::string ARC_FRAGMENT_SHADER_MAIN;

        // How far inside the far plane an infinitely distant object is placed. Far enough that the
        // map is always in front of it, close enough that it never clips.
        static const double INFINITE_DISTANCE_FACTOR;

        std::shared_ptr<Shader> _spriteShader;
        std::string _fogShaderSource;   // the fog block both programs were built with
        std::shared_ptr<Shader> _arcShader;
        std::weak_ptr<Options> _options;
        std::weak_ptr<MapRenderer> _mapRenderer;

        std::vector<std::shared_ptr<CelestialObject> > _objects;

        std::vector<float> _coordBuf;
        std::vector<unsigned char> _colorBuf;
        std::vector<float> _texCoordBuf;
        std::vector<unsigned short> _indexBuf;

        mutable std::mutex _mutex;
    };

}

#endif
