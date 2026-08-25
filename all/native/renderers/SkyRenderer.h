/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SKYRENDERER_H_
#define _MASSIF_SKYRENDERER_H_

#include "renderers/utils/GLContext.h"
#include "components/SkyOptions.h"
#include "components/StyleEnvironment.h"

#include <chrono>
#include <memory>
#include <string>

namespace massif {
    class Options;
    class Shader;
    class GLResourceManager;
    class ViewState;

    /**
     * Draws the sky as a single full-screen pass with a per-pixel world-space view ray.
     * The fragment shader is the scattering atmosphere, the older two-colour gradient, or user
     * GLSL supplied through SkyOptions::setShaderSource.
     */
    class SkyRenderer {
    public:
        explicit SkyRenderer(const Options& options);
        virtual ~SkyRenderer();

        void onSurfaceCreated(const std::shared_ptr<GLResourceManager>& resourceManager);
        /**
         * Draws the sky. Returns true if anything was drawn, in which case the legacy sky
         * band must not be drawn on top of it. The fog is resolved by the owner and shared with
         * the ground, so the two meet at the horizon whether it came from the options or a style.
         */
        bool onDrawFrame(const ViewState& viewState, const ResolvedFog& fog, const ResolvedSky& sky);
        void onSurfaceDestroyed();

    protected:
        bool updateShader(const ResolvedSky& sky);

        static const std::string SKY_VERTEX_SHADER;
        static const std::string SKY_FRAGMENT_SHADER_PREFIX;
        static const std::string SKY_FRAGMENT_SHADER_COMMON;
        static const std::string SKY_FRAGMENT_SHADER_MAIN;
        static const std::string SKY_FRAGMENT_SHADER_GRADIENT;
        static const std::string SKY_FRAGMENT_SHADER_ATMOSPHERE;
        static const std::string SKY_FRAGMENT_SHADER_SCATTERING;

        static const float QUAD_COORDS[8];
        // How far below the horizon the sky quad still reaches, in normalized device units: the sky
        // shader carries its fog band below the skyline.
        static const float SKY_HORIZON_MARGIN;
        static bool isHorizonClipEnabled();

        std::shared_ptr<Shader> _shader;
        std::string _shaderSource;      // the SkyOptions source the current shader was built from
        std::string _fogShaderSource;   // the FogOptions source it was built with
        SkyType::SkyType _shaderType;   // ... and the type and quality, both compiled in
        SkyQuality::SkyQuality _shaderQuality;
        bool _shaderFailed;             // custom source failed to compile; do not retry it

        // Uniform locations are queried directly, not through Shader::getUniformLoc: a custom
        // sky shader may not reference every uniform of the contract, the GLSL compiler then
        // removes it, and Shader::getUniformLoc reports the missing uniform as location 0 -
        // which would silently overwrite whichever uniform really is at location 0.
        GLuint _a_coord;
        GLint _u_invMVPMat;
        GLint _u_sunDir;
        GLint _u_sunColor;
        GLint _u_skyColor;
        GLint _u_horizonColor;
        GLint _u_groundColor;
        GLint _u_horizonBlend;
        GLint _u_sunIntensity;
        GLint _u_sunDisc;
        GLint _u_atmosphere;
        GLint _u_atmosphereColor;
        GLint _u_haloColor;
        GLint _u_time;
        GLint _u_zoom;
        GLint _u_cameraHeight;
        GLint _u_resolution;
        GLint _u_starIntensity;

        std::chrono::steady_clock::time_point _startTime;

        std::shared_ptr<GLResourceManager> _glResourceManager;

        const Options& _options;
    };

}

#endif
