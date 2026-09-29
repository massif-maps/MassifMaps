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
    class BitmapTextureCache;
    class CelestialLabel;
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
     * Draws the objects of a CelestialLayer: images as meshes on the sky sphere, sprites batched by
     * bitmap, arcs widened in the vertex shader (line width is ignored on WebGL). Depth-tested but never depth-writing, so the map covers them;
     * occludedByMap off disables the test.
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
        struct SpriteInstance {
            std::shared_ptr<CelestialObject> object;
            cglib::vec3<double> worldPos;
            float halfWidth;                // world units at worldPos
            float halfHeight;
            float shiftRight;               // the quad's centre off worldPos, world units, on screen
            float shiftUp;
            float softness;
            bool occluded;
            unsigned char color[4];
            std::shared_ptr<Bitmap> bitmap;
        };

        bool initializeRenderer();
        void setupFogUniforms(GLuint progId, const ViewState& viewState) const;
        bool resolveWorldPos(const std::shared_ptr<CelestialObject>& object, const ViewState& viewState, cglib::vec3<double>& worldPos, double& distance) const;
        void buildSprites(const ViewState& viewState, float opacity, std::vector<SpriteInstance>& instances) const;
        bool buildLabel(const std::shared_ptr<CelestialLabel>& label, const ViewState& viewState, double distance, SpriteInstance& instance) const;
        void drawSprites(const std::vector<SpriteInstance>& instances, const ViewState& viewState);
        void drawArcs(const ViewState& viewState, float opacity);
        void drawImages(const ViewState& viewState, float opacity);
        void calculateRayIntersectedArcs(const std::shared_ptr<CelestialLayer>& layer, const cglib::ray3<double>& ray, const cglib::vec3<double>& rayDir, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const;

        static const std::string SPRITE_VERTEX_SHADER;
        static const std::string SPRITE_FRAGMENT_SHADER_PREFIX;
        static const std::string SPRITE_FRAGMENT_SHADER_MAIN;
        static const std::string CELESTIAL_FRAGMENT_SHADER_FOG;
        static const std::string ARC_VERTEX_SHADER;
        static const std::string ARC_FRAGMENT_SHADER_PREFIX;
        static const std::string ARC_FRAGMENT_SHADER_MAIN;

        // Fraction of the far plane an infinitely distant object sits at: behind the map, never clipped.
        static const double INFINITE_DISTANCE_FACTOR;
        static const int IMAGE_SUBDIVISIONS;
        static const unsigned int IMAGE_TEXTURE_CACHE_SIZE;

        std::shared_ptr<Shader> _spriteShader;
        std::string _fogShaderSource;   // the fog block both programs were built with
        std::shared_ptr<Shader> _arcShader;
        std::shared_ptr<BitmapTextureCache> _imageTextureCache;
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
