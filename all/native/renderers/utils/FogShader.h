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
     * The one fog implementation (Mapbox's _prelude_fog), shared by every fogging renderer so ground and sky meet
     * without a seam. vt cannot include all/native: GLTileRendererShaders.h keeps a verbatim copy, any
     * difference is a bug. See docs/internals/rendering/08-lighting-sky-fog.md. Internal.
     */
    namespace FogShader {
        /**
         * The uniform block, always declared, so a custom fog shader must not redeclare any of it.
         */
        extern const std::string UNIFORMS;

        /**
         * fogRayVec / fogRange / fogOpacity / fogHorizonBlend / fogVertical: the model, always the SDK's.
         */
        extern const std::string HELPERS;

        /**
         * applyFog(color, dir, dist, heightM), skyFog(color, dir) and fogLabelFade(); a custom fog shader
         * replaces these.
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
         * Builds the fragment-shader fog section: uniforms, helpers, custom or built-in blends, call site.
         * @param customSource FogOptions::getShaderSource(), or empty for the built-in blend.
         */
        std::string buildBlock(const std::string& customSource);

        /**
         * The application's fog shader source, or empty; renderers rebuild their program when it changes.
         */
        std::string source(const std::shared_ptr<Options>& options);

        /**
         * rayVec = uFogRay * vec3(gl_FragCoord.xy, 1) is the world-space z-up pixel ray, unit along the view
         * axis: length(rayVec) / gl_FragCoord.w is the camera distance.
         */
        cglib::mat3x3<float> rayBasis(const ViewState& viewState);

        /**
         * Uploads the uniform block; guarded locations, so a program with fog stripped out costs nothing.
         */
        void setUniforms(GLuint progId, const ResolvedFog& fog, const ViewState& viewState);

        /**
         * Mapbox's floor on horizon-blend: the term divides by it.
         */
        extern const float MIN_HORIZON_BLEND;
    }

}

#endif
