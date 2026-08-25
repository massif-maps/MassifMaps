#include "FogShader.h"
#include "components/FogOptions.h"
#include "components/Options.h"
#include "components/StyleEnvironment.h"
#include "graphics/ViewState.h"
#include "utils/Const.h"

#include <algorithm>

namespace massif {

    namespace FogShader {

        const float MIN_HORIZON_BLEND = 0.0005f;

        const std::string UNIFORMS = R"GLSL(
            uniform lowp vec4 uFogColor;      // rgb = fog colour, a = how opaque the fog gets at full distance
            uniform lowp vec4 uFogHighColor;  // the upper atmosphere; transparent leaves the sky alone
            uniform lowp vec4 uFogSpaceColor; // the zenith, beyond the atmosphere
            uniform highp vec4 uFogParams;    // range start, 1 / (end - start), internal -> range units, horizon blend
            uniform highp vec4 uFogVertical;  // fade-out start and end in metres, metres per internal unit, camera height in metres
            uniform highp mat3 uFogRay;       // view ray basis, see FogShader::rayBasis
        )GLSL";

        // Mapbox's model. The distance ramp is theirs verbatim in shape (an exponential decay,
        // cubed to soften the onset); the horizon term is the piece that matters - the GROUND takes
        // it too, so a ridge at +5 degrees is fogged exactly as much as the sky just above it and
        // the two meet without a seam. Below the horizon dir.z is negative and the term is 1, which
        // is plain distance fog.
        const std::string HELPERS = R"GLSL(
            // The unnormalised world-space, z-up ray through this fragment. uFogRay is scaled so
            // its projection on the view axis is 1, so length(rayVec) / gl_FragCoord.w is the TRUE
            // distance from the camera - which the depth alone is not, being short by up to the
            // half-diagonal of the frustum at the screen corners.
            highp vec3 fogRayVec() {
                return uFogRay * vec3(gl_FragCoord.x, gl_FragCoord.y, 1.0);
            }

            highp float fogRange(highp float dist) {
                return (dist - uFogParams.x) * uFogParams.y;
            }

            lowp float fogOpacity(highp float t) {
                lowp float falloff = 1.0 - min(1.0, exp(-6.0 * t));
                falloff *= falloff * falloff;
                return uFogColor.a * min(1.0, 1.00747 * falloff);
            }

            lowp float fogHorizonBlend(highp vec3 dir) {
                highp float t = max(0.0, dir.z / uFogParams.w);
                // Factor 3 matches a smoothstep over the same width.
                return exp(-3.0 * t * t);
            }

            // How much of the fog a fragment at this height escapes - 1 above the range, so a summit
            // stands clear of a valley haze.
            lowp float fogVertical(highp float heightM) {
                return uFogVertical.y > uFogVertical.x ? smoothstep(uFogVertical.x, uFogVertical.y, heightM) : 0.0;
            }
        )GLSL";

        // Colours are PREMULTIPLIED, so the fog colour is premultiplied by the fragment's own alpha;
        // the fog tints what is there rather than adding coverage, so alpha is left alone.
        const std::string BUILTIN = R"GLSL(
            lowp vec4 applyFog(lowp vec4 color, highp vec3 dir, highp float dist, highp float heightM) {
                lowp float amount = fogOpacity(fogRange(dist)) * fogHorizonBlend(dir);
                amount *= 1.0 - fogVertical(heightM);
                return vec4(mix(color.rgb, uFogColor.rgb * color.a, amount), color.a);
            }

            // The sky is at infinity, so only the angular term varies over it.
            lowp vec4 skyFog(lowp vec4 color, highp vec3 dir) {
                lowp float amount = uFogColor.a * fogHorizonBlend(dir);
                return vec4(mix(color.rgb, uFogColor.rgb * color.a, amount), color.a);
            }

            // What a label keeps once the fog has swallowed the map under it - a label with nothing
            // behind it reads as floating text (mapbox clips its symbols at the same 0.9).
            lowp float fogLabelFade() {
                highp vec3 rayVec = fogRayVec();
                highp float dist = length(rayVec) / max(1.0e-9, gl_FragCoord.w) * uFogParams.z;
                return 1.0 - smoothstep(0.9, 1.0, fogOpacity(fogRange(dist)));
            }
        )GLSL";

        // The call site every renderer uses, in terms of the entry point a custom shader supplies.
        const std::string WRAPPER = R"GLSL(
            lowp vec4 applyFog(lowp vec4 color) {
                highp vec3 rayVec = fogRayVec();
                highp float rayLen = length(rayVec);
                highp vec3 dir = rayVec / rayLen;
                highp float dist = rayLen / max(1.0e-9, gl_FragCoord.w);
                highp float heightM = uFogVertical.w + dist * dir.z * uFogVertical.z;
                return applyFog(color, dir, dist * uFogParams.z, heightM);
            }
        )GLSL";

        const std::string DISABLED = R"GLSL(
            lowp vec4 applyFog(lowp vec4 color) {
                return color;
            }
            lowp float fogLabelFade() {
                return 1.0;
            }
        )GLSL";

        // The uniforms and the helpers are always the SDK's: they are the model itself, and three
        // renderers would otherwise each carry their own copy of the same two lines. What a custom
        // source replaces is every BLEND - applyFog, skyFog and fogLabelFade - which is where the
        // appearance actually lives. A custom source is free to ignore the helpers entirely.
        std::string buildBlock(const std::string& customSource) {
            return UNIFORMS + HELPERS + (customSource.empty() ? BUILTIN : customSource) + WRAPPER;
        }

        std::string source(const std::shared_ptr<Options>& options) {
            if (options) {
                if (std::shared_ptr<FogOptions> fogOptions = options->getFogOptions()) {
                    return fogOptions->getShaderSource();
                }
            }
            return std::string();
        }

        cglib::mat3x3<float> rayBasis(const ViewState& viewState) {
            cglib::mat4x4<float> invMVPMat = cglib::inverse(viewState.getRTEModelviewProjectionMat());

            // The modelview is relative to the eye, so a point unprojected on the near plane IS the
            // view ray. The near plane is flat in eye space, so the ray is an affine function of the
            // pixel and three unprojections determine it.
            auto unproject = [&invMVPMat](float ndcX, float ndcY) {
                return cglib::transform_point(cglib::vec3<float>(ndcX, ndcY, -1.0f), invMVPMat);
            };

            float width = static_cast<float>(std::max(1, viewState.getWidth()));
            float height = static_cast<float>(std::max(1, viewState.getHeight()));
            cglib::vec3<float> origin = unproject(-1.0f, -1.0f);
            cglib::vec3<float> dX = unproject(-1.0f + 2.0f / width, -1.0f) - origin;
            cglib::vec3<float> dY = unproject(-1.0f, -1.0f + 2.0f / height) - origin;

            // Every near-plane point sits at the near distance along the view axis, so dividing by
            // the centre ray's length is what makes that projection exactly 1.
            float near = static_cast<float>(cglib::length(unproject(0.0f, 0.0f)));
            float scale = 1.0f / std::max(1.0e-9f, near);

            cglib::mat3x3<float> basis = cglib::mat3x3<float>::zero();
            for (int row = 0; row < 3; row++) {
                basis(row, 0) = dX(row) * scale;
                basis(row, 1) = dY(row) * scale;
                basis(row, 2) = origin(row) * scale;
            }
            return basis;
        }

        void setUniforms(GLuint progId, const ResolvedFog& fog, const ViewState& viewState) {
            GLint loc = glGetUniformLocation(progId, "uFogColor");
            if (loc >= 0) {
                glUniform4f(loc, fog.color.getR() / 255.0f, fog.color.getG() / 255.0f, fog.color.getB() / 255.0f, fog.color.getA() / 255.0f);
            }
            if ((loc = glGetUniformLocation(progId, "uFogHighColor")) >= 0) {
                glUniform4f(loc, fog.highColor.getR() / 255.0f, fog.highColor.getG() / 255.0f, fog.highColor.getB() / 255.0f, fog.highColor.getA() / 255.0f);
            }
            if ((loc = glGetUniformLocation(progId, "uFogSpaceColor")) >= 0) {
                glUniform4f(loc, fog.spaceColor.getR() / 255.0f, fog.spaceColor.getG() / 255.0f, fog.spaceColor.getB() / 255.0f, fog.spaceColor.getA() / 255.0f);
            }
            if ((loc = glGetUniformLocation(progId, "uFogParams")) >= 0) {
                glUniform4f(loc, fog.rangeStart, 1.0f / std::max(1.0e-9f, fog.rangeEnd - fog.rangeStart), 1.0f / fog.rangeScale,
                            std::max(MIN_HORIZON_BLEND, fog.horizonBlend));
            }
            if ((loc = glGetUniformLocation(progId, "uFogVertical")) >= 0) {
                float metersPerUnit = static_cast<float>(Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE);
                glUniform4f(loc, fog.verticalRangeStart, fog.verticalRangeEnd, metersPerUnit,
                            static_cast<float>(viewState.getCameraPos()(2)) * metersPerUnit);
            }
            if ((loc = glGetUniformLocation(progId, "uFogRay")) >= 0) {
                cglib::mat3x3<float> basis = rayBasis(viewState);
                glUniformMatrix3fv(loc, 1, GL_FALSE, basis.data());
            }
        }

    }

}
