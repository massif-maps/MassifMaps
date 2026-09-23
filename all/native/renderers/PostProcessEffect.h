/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_POSTPROCESSEFFECT_H_
#define _MASSIF_POSTPROCESSEFFECT_H_

#include "graphics/Color.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace massif {
    /**
     * A full-screen post-processing effect. When attached to the map via
     * MapRenderer::setPostProcessEffect, the map is rendered into an offscreen buffer
     * and the effect fragment shader produces the final screen output.
     *
     * The fragment shader must be GLSL ES 1.00 (#version 100) source. The following
     * uniforms are provided by the renderer:
     * - sampler2D uColorTex: the rendered map frame (premultiplied alpha).
     * - sampler2D uTerrainDepthTex: packed terrain depth buffer (only if terrain depth is required;
     *   RGB = 24-bit fixed point linear depth 0..1 relative to the far plane, A = terrain coverage).
     *   Use dot(rgb, vec3(1.0, 1.0/255.0, 1.0/65025.0)) to unpack.
     * - vec2 uInvScreenSize: 1/width, 1/height of the screen in pixels.
     * - float uNear, uFar: view frustum distances (internal units).
     * - vec2 uProjInvScale: tan(fovy/2) * aspect, tan(fovy/2). With the terrain depth this
     *   reconstructs the eye-space position of a pixel:
     *   vec3(ndc * uProjInvScale, -1.0) * depth * uFar, ndc = uv * 2 - 1.
     * - float uTime: seconds since the effect was attached.
     * Additionally all float parameters set via setFloatParameter are available as float
     * uniforms, and all colors set via setColorParameter as vec4 uniforms (rgba 0..1).
     * Screen texture coordinates can be computed as gl_FragCoord.xy * uInvScreenSize.
     *
     * Note: this class is experimental and may change or even be removed in future SDK versions.
     */
    class PostProcessEffect {
    public:
        /**
         * Constructs a post-process effect from a fragment shader.
         * @param name The name of the effect (used for shader caching, should be unique).
         * @param fragmentShader The GLSL ES 1.00 fragment shader source.
         */
        PostProcessEffect(const std::string& name, const std::string& fragmentShader);
        virtual ~PostProcessEffect();

        /**
         * Returns the name of the effect.
         * @return The name of the effect.
         */
        const std::string& getName() const;
        /**
         * Returns the fragment shader source of the effect.
         * @return The fragment shader source.
         */
        const std::string& getFragmentShader() const;

        /**
         * Returns true if the effect needs the terrain depth pre-pass (uTerrainDepthTex).
         * @return True if terrain depth is rendered for the effect. The default is false.
         */
        bool isTerrainDepthRequired() const;
        /**
         * Sets whether the effect needs the terrain depth pre-pass.
         * @param required True if the terrain depth should be rendered for the effect.
         */
        void setTerrainDepthRequired(bool required);

        /**
         * Returns true if the effect wants the terrain SURFACE NORMAL in the depth pre-pass.
         * @return True if the pre-pass packs normals. The default is false.
         */
        bool isTerrainNormalsRequired() const;
        /**
         * Sets whether the effect wants the terrain surface normal in the depth pre-pass.
         *
         * This CHANGES THE LAYOUT of uTerrainDepthTex, so an effect that asks for it has to decode
         * the other one. Implies setTerrainDepthRequired: there is one texture, not two.
         *
         *   off (the default): RGB = 24-bit LINEAR depth over the far plane, A = 1 on terrain and
         *                      0 on sky.
         *   on:                RG  = 16-bit SQRT depth (decode dot(rg, vec2(1.0, 1.0/255.0)) and
         *                      SQUARE it), BA = the surface normal, octahedral, z up.
         *
         *     float enc    = dot(texel.rg, vec2(1.0, 1.0 / 255.0));
         *     float depth  = enc * enc;             // 0..1 over the far plane
         *     bool  sky    = enc >= 1.0;            // no coverage channel: 1 IS the sky
         *     vec2  oct    = texel.ba * 2.0 - 1.0;
         *     vec3  normal = normalize(vec3(oct, 1.0 - abs(oct.x) - abs(oct.y)));
         *
         * Why the depth gets shorter and curved rather than simply losing a byte: an effect reads
         * this buffer for its GRADIENT, and a linear 16-bit depth over a 300 km far plane quantises
         * the near field into a handful of steps. The square root spends the bits where the picture
         * is - sub-metre at a hundred metres, ten-odd metres at the far plane - and a threshold
         * expressed against it is naturally relative, which is what an edge test wants anyway.
         *
         * The normal is the MESH's, the same per-vertex world-space normal the surface shader is
         * given, so it is continuous where the mesh is and does not have to be guessed back out of
         * the depth. That is the point: a normal reconstructed from a half-resolution depth cannot
         * tell a ridge from the LOD kink where two tiles of different resolution meet, and inks
         * both. A height field's normal always points up, so the octahedral encoding is exact here
         * and needs no hemisphere fold.
         *
         * @param required True if the pre-pass should pack normals.
         */
        void setTerrainNormalsRequired(bool required);

        /**
         * Returns the value of a float parameter.
         * @param name The name of the parameter.
         * @return The value of the parameter, or 0 if not set.
         */
        float getFloatParameter(const std::string& name) const;
        /**
         * Sets a float parameter. The parameter is exposed to the fragment shader as a uniform.
         * @param name The name of the parameter (must be a valid GLSL identifier).
         * @param value The new value for the parameter.
         */
        void setFloatParameter(const std::string& name, float value);

        /**
         * Returns the value of a color parameter.
         * @param name The name of the parameter.
         * @return The value of the parameter, or transparent black if not set.
         */
        Color getColorParameter(const std::string& name) const;
        /**
         * Sets a color parameter. The parameter is exposed to the fragment shader as a vec4
         * uniform with components in the 0..1 range.
         * @param name The name of the parameter (must be a valid GLSL identifier).
         * @param color The new value for the parameter.
         */
        void setColorParameter(const std::string& name, const Color& color);

        /**
         * Returns all float parameters. Internal method.
         * @return The map of all parameters.
         */
        std::map<std::string, float> getFloatParameters() const;
        /**
         * Returns all color parameters. Internal method.
         * @return The map of all parameters.
         */
        std::map<std::string, Color> getColorParameters() const;


    private:
        const std::string _name;
        const std::string _fragmentShader;

        bool _terrainDepthRequired;
        bool _terrainNormalsRequired;
        std::map<std::string, float> _floatParameters;
        std::map<std::string, Color> _colorParameters;

        mutable std::mutex _mutex;
    };
}

#endif
