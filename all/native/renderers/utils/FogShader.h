/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FOGSHADER_H_
#define _MASSIF_FOGSHADER_H_

#include "renderers/utils/GLContext.h"

#include <memory>
#include <string>

#include <cglib/mat.h>

namespace massif {
    struct ResolvedFog;
    class Options;
    class ViewState;

    /**
     * The one fog implementation. Every renderer that fogs - the vt tile content, the background
     * plane, the terrain surface, the sky, the celestial objects and the vector elements - is built
     * from these strings, so ground and sky can never drift apart.
     *
     * vt lives in another repository and cannot include this header: GLTileRendererShaders.h keeps
     * a verbatim copy and names this file as the master. A difference between the two is a bug.
     *
     * The model is Mapbox's (their _prelude_fog): a distance ramp multiplied by an angular horizon
     * term that BOTH the ground and the sky take, which is what makes the two meet without a seam.
     * See docs/internals/rendering/08-lighting-sky-fog.md.
     *
     * Internal class, not exposed through the public API.
     */
    namespace FogShader {
        /**
         * The uniform block. Always declared by the SDK, also in front of a custom fog shader -
         * which is why a custom shader must not redeclare any of these.
         */
        extern const std::string UNIFORMS;

        /**
         * fogRayVec / fogRange / fogOpacity / fogHorizonBlend / fogVertical - the model itself,
         * always the SDK's, so there is one definition of it. A custom shader may use them or not.
         */
        extern const std::string HELPERS;

        /**
         * The built-in blends - applyFog(color, dir, dist, heightM), skyFog(color, dir) and
         * fogLabelFade(). This is what a custom fog shader replaces.
         */
        extern const std::string BUILTIN;

        /**
         * applyFog(color), the call site every renderer uses, in terms of the entry point above.
         */
        extern const std::string WRAPPER;

        /**
         * applyFog(color) as a no-op, for the programs compiled without fog.
         */
        extern const std::string DISABLED;

        /**
         * Builds the fragment-shader fog section: the uniforms and helpers, then either the custom
         * blends or the built-in ones, then the gl_FragCoord call site.
         * @param customSource FogOptions::getShaderSource(), or empty for the built-in blend.
         */
        std::string buildBlock(const std::string& customSource);

        /**
         * The application's fog shader source, or an empty string when there is none. For the
         * renderers that compile the fog into their own program and rebuild it when it changes.
         */
        std::string source(const std::shared_ptr<Options>& options);

        /**
         * The view ray basis: rayVec = uFogRay * vec3(gl_FragCoord.xy, 1) is the world-space,
         * z-up ray through the pixel, scaled so its projection on the view axis is exactly 1.
         * Then length(rayVec) / gl_FragCoord.w is the true distance from the camera, and
         * normalize(rayVec) is the direction the horizon term needs.
         */
        cglib::mat3x3<float> rayBasis(const ViewState& viewState);

        /**
         * Uploads the whole uniform block. Locations are looked up by name with glGetUniformLocation
         * and guarded with >= 0, so a program the compiler stripped the fog out of costs nothing.
         */
        void setUniforms(GLuint progId, const ResolvedFog& fog, const ViewState& viewState);

        /**
         * Mapbox's floor on horizon-blend: the term divides by it, and 0 would mean "no fog in the
         * sky at all" rather than "a sharp edge at the horizon".
         */
        extern const float MIN_HORIZON_BLEND;
    }

}

#endif
