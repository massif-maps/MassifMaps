#include "SkyRenderer.h"
#include "components/Options.h"
#include "components/LightOptions.h"
#include "components/SkyOptions.h"
#include "components/FogOptions.h"
#include "components/StyleEnvironment.h"
#include "components/TerrainOptions.h"
#include "graphics/ViewState.h"
#include "renderers/utils/FogShader.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/Shader.h"
#include "utils/Const.h"
#include "utils/Log.h"

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

#include <algorithm>
#include <cmath>

#include <cglib/mat.h>

namespace massif {

    SkyRenderer::SkyRenderer(const Options& options) :
        _shader(),
        _shaderSource(),
        _fogShaderSource(),
        _shaderType(SkyType::SKY_TYPE_ATMOSPHERE),
        _shaderQuality(SkyQuality::SKY_QUALITY_MEDIUM),
        _shaderFailed(false),
        _a_coord(0),
        _u_invMVPMat(-1),
        _u_sunDir(-1),
        _u_sunColor(-1),
        _u_skyColor(-1),
        _u_horizonColor(-1),
        _u_groundColor(-1),
        _u_horizonBlend(-1),
        _u_sunIntensity(-1),
        _u_sunDisc(-1),
        _u_atmosphere(-1),
        _u_atmosphereColor(-1),
        _u_haloColor(-1),
        _u_time(-1),
        _u_zoom(-1),
        _u_cameraHeight(-1),
        _u_resolution(-1),
        _u_starIntensity(-1),
        _startTime(std::chrono::steady_clock::now()),
        _glResourceManager(),
        _options(options)
    {
    }

    SkyRenderer::~SkyRenderer() {
    }

    void SkyRenderer::onSurfaceCreated(const std::shared_ptr<GLResourceManager>& resourceManager) {
        _glResourceManager = resourceManager;
        _shader.reset();
        _shaderSource.clear();
        _shaderFailed = false;
    }

    void SkyRenderer::onSurfaceDestroyed() {
        _shader.reset();
        _shaderSource.clear();
        _shaderFailed = false;
        _glResourceManager.reset();
    }

    bool SkyRenderer::updateShader(const ResolvedSky& sky) {
        std::shared_ptr<SkyOptions> skyOptions = _options.getSkyOptions();
        std::shared_ptr<FogOptions> fogOptions = _options.getFogOptions();
        std::string source = skyOptions ? skyOptions->getShaderSource() : std::string();
        std::string fogSource = fogOptions ? fogOptions->getShaderSource() : std::string();
        SkyType::SkyType type = sky.type;
        SkyQuality::SkyQuality quality = skyOptions ? skyOptions->getQuality() : SkyQuality::SKY_QUALITY_MEDIUM;
        bool same = _shaderSource == source && _fogShaderSource == fogSource && _shaderType == type && _shaderQuality == quality;
        if (_shader && same) {
            return true;
        }

        // A custom source that failed to compile once must not be retried every frame.
        if (_shaderFailed && same) {
            return static_cast<bool>(_shader);
        }

        _shaderSource = source;
        _fogShaderSource = fogSource;
        _shaderType = type;
        _shaderQuality = quality;
        _shaderFailed = false;

        // The sample counts are compiled in so both loops unroll: the whole cost of the atmosphere
        // is this pair, and a driver will not unroll a loop bounded by a uniform.
        int steps = 8, lightSteps = 4;
        if (quality == SkyQuality::SKY_QUALITY_LOW) {
            steps = 5;
            lightSteps = 3;
        } else if (quality == SkyQuality::SKY_QUALITY_HIGH) {
            steps = 12;
            lightSteps = 5;
        }
        std::string scattering;
        if (type == SkyType::SKY_TYPE_ATMOSPHERE) {
            scattering = "#define ATMO_STEPS " + std::to_string(steps) + "\n"
                       + "#define ATMO_LIGHT_STEPS " + std::to_string(lightSteps) + "\n"
                       + SKY_FRAGMENT_SHADER_SCATTERING;
        }
        std::string builtin = (type == SkyType::SKY_TYPE_ATMOSPHERE ? SKY_FRAGMENT_SHADER_ATMOSPHERE : SKY_FRAGMENT_SHADER_GRADIENT);
        std::string prefix = SKY_FRAGMENT_SHADER_PREFIX + FogShader::buildBlock(fogSource) + SKY_FRAGMENT_SHADER_COMMON + scattering;
        std::string body = source.empty() ? builtin : source;
        std::shared_ptr<Shader> shader = _glResourceManager->create<Shader>("sky", SKY_VERTEX_SHADER, prefix + body + SKY_FRAGMENT_SHADER_MAIN);
        if (shader->getProgId() == 0 && !(source.empty() && fogSource.empty())) {
            Log::Errorf("SkyRenderer::updateShader: the custom %s shader failed to compile, falling back to the built-in shaders",
                        source.empty() ? "fog" : (fogSource.empty() ? "sky" : "sky and fog"));
            _shaderFailed = true;
            shader = _glResourceManager->create<Shader>("sky", SKY_VERTEX_SHADER,
                                                        SKY_FRAGMENT_SHADER_PREFIX + FogShader::buildBlock(std::string()) + SKY_FRAGMENT_SHADER_COMMON + scattering + builtin + SKY_FRAGMENT_SHADER_MAIN);
        }
        if (shader->getProgId() == 0) {
            _shader.reset();
            return false;
        }

        _shader = shader;
        GLuint progId = _shader->getProgId();
        _a_coord = _shader->getAttribLoc("a_coord");
        _u_invMVPMat = glGetUniformLocation(progId, "u_invMVPMat");
        _u_sunDir = glGetUniformLocation(progId, "u_sunDir");
        _u_sunColor = glGetUniformLocation(progId, "u_sunColor");
        _u_skyColor = glGetUniformLocation(progId, "u_skyColor");
        _u_horizonColor = glGetUniformLocation(progId, "u_horizonColor");
        _u_groundColor = glGetUniformLocation(progId, "u_groundColor");
        _u_horizonBlend = glGetUniformLocation(progId, "u_horizonBlend");
        _u_sunIntensity = glGetUniformLocation(progId, "u_sunIntensity");
        _u_sunDisc = glGetUniformLocation(progId, "u_sunDisc");
        _u_atmosphere = glGetUniformLocation(progId, "u_atmosphere");
        _u_atmosphereColor = glGetUniformLocation(progId, "u_atmosphereColor");
        _u_haloColor = glGetUniformLocation(progId, "u_haloColor");
        _u_time = glGetUniformLocation(progId, "u_time");
        _u_zoom = glGetUniformLocation(progId, "u_zoom");
        _u_cameraHeight = glGetUniformLocation(progId, "u_cameraHeight");
        _u_resolution = glGetUniformLocation(progId, "u_resolution");
        _u_starIntensity = glGetUniformLocation(progId, "u_starIntensity");
        return true;
    }

    // Measurement switch: debug.massif.skyclip 0 draws the sky over the whole screen again, which
    // is what it did before the quad was clipped to the horizon. Read once (Android only).
#ifdef __ANDROID__
    bool SkyRenderer::isHorizonClipEnabled() {
        static const bool enabled = [] {
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.skyclip", property) > 0 && property[0] == '0');
        }();
        return enabled;
    }
#else
    bool SkyRenderer::isHorizonClipEnabled() {
        return true;
    }
#endif

    bool SkyRenderer::onDrawFrame(const ViewState& viewState, const ResolvedFog& fog, const ResolvedSky& sky) {
        std::shared_ptr<SkyOptions> skyOptions = _options.getSkyOptions();
        if (!skyOptions || !skyOptions->isEnabled() || !_glResourceManager) {
            return false;
        }

        // With 3D terrain the sky can be exposed by a peak even when the horizon plane is not
        // in view, so the cheap horizon test is only used when the terrain is flat.
        if (!viewState.isSkyVisible()) {
            std::shared_ptr<TerrainOptions> terrainOptions = _options.getTerrainOptions();
            if (!terrainOptions || !terrainOptions->isActive()) {
                return false;
            }
        }

        if (!updateShader(sky)) {
            return false;
        }

        std::shared_ptr<LightOptions> lightOptions = _options.getLightOptions();
        cglib::vec3<float> sunDir(0.0f, 0.0f, 1.0f);
        Color sunColor(255, 255, 255, 255);
        float sunIntensity = 1.0f;
        if (lightOptions) {
            sunDir = lightOptions->getSunDirection();
            sunColor = lightOptions->getSunColor();
            sunIntensity = lightOptions->getSunIntensity();
        }

        Color skyColor = skyOptions->getSkyColor();
        Color horizonColor = skyOptions->getHorizonColor();
        Color groundColor = skyOptions->getGroundColor();
        Color atmosphereColor = sky.atmosphereColor;
        Color haloColor = sky.haloColor;

        cglib::mat4x4<float> invMVPMat = cglib::inverse(viewState.getRTEModelviewProjectionMat());

        glUseProgram(_shader->getProgId());
        // The fog comes from the owner, resolved once for the frame from the same options AND the
        // same style environment the ground gets - resolving it here from an empty environment is
        // what left a style-declared fog on the map and out of the sky. There is no angle to
        // reconcile any more: the sky and the ground take the same horizon term.
        //
        // Passed WHOLE, not zeroed when the fog is off the way the ground renderers zero it: the
        // atmosphere colours and the star intensity ride on FogOptions but belong to the sky, and
        // an off switch that took the stars and the dusk sky with it was a bug. resolveFog drops
        // the fog colour instead, so uFogColor.a is 0 and skyFog is already a no-op.
        FogShader::setUniforms(_shader->getProgId(), fog, viewState);
        if (_u_invMVPMat >= 0) {
            glUniformMatrix4fv(_u_invMVPMat, 1, GL_FALSE, invMVPMat.data());
        }
        if (_u_sunDir >= 0) {
            glUniform3fv(_u_sunDir, 1, sunDir.data());
        }
        if (_u_sunColor >= 0) {
            glUniform4f(_u_sunColor, sunColor.getR() / 255.0f, sunColor.getG() / 255.0f, sunColor.getB() / 255.0f, sunColor.getA() / 255.0f);
        }
        if (_u_skyColor >= 0) {
            glUniform4f(_u_skyColor, skyColor.getR() / 255.0f, skyColor.getG() / 255.0f, skyColor.getB() / 255.0f, skyColor.getA() / 255.0f);
        }
        if (_u_horizonColor >= 0) {
            glUniform4f(_u_horizonColor, horizonColor.getR() / 255.0f, horizonColor.getG() / 255.0f, horizonColor.getB() / 255.0f, horizonColor.getA() / 255.0f);
        }
        if (_u_groundColor >= 0) {
            glUniform4f(_u_groundColor, groundColor.getR() / 255.0f, groundColor.getG() / 255.0f, groundColor.getB() / 255.0f, groundColor.getA() / 255.0f);
        }
        if (_u_horizonBlend >= 0) {
            glUniform1f(_u_horizonBlend, static_cast<float>(skyOptions->getHorizonBlend() * Const::DEG_TO_RAD));
        }
        if (_u_sunIntensity >= 0) {
            glUniform1f(_u_sunIntensity, sunIntensity);
        }
        if (_u_sunDisc >= 0) {
            glUniform1f(_u_sunDisc, skyOptions->isSunDiscEnabled() ? 1.0f : 0.0f);
        }
        if (_u_atmosphere >= 0) {
            glUniform4f(_u_atmosphere, sky.atmosphereSunIntensity, sky.atmosphereLuminance, 0.0f, 0.0f);
        }
        if (_u_atmosphereColor >= 0) {
            glUniform4f(_u_atmosphereColor, atmosphereColor.getR() / 255.0f, atmosphereColor.getG() / 255.0f, atmosphereColor.getB() / 255.0f, atmosphereColor.getA() / 255.0f);
        }
        if (_u_haloColor >= 0) {
            glUniform4f(_u_haloColor, haloColor.getR() / 255.0f, haloColor.getG() / 255.0f, haloColor.getB() / 255.0f, haloColor.getA() / 255.0f);
        }
        if (_u_time >= 0) {
            glUniform1f(_u_time, std::chrono::duration_cast<std::chrono::duration<float> >(std::chrono::steady_clock::now() - _startTime).count());
        }
        if (_u_zoom >= 0) {
            glUniform1f(_u_zoom, viewState.getZoom());
        }
        if (_u_cameraHeight >= 0) {
            glUniform1f(_u_cameraHeight, static_cast<float>(viewState.getCameraPos()(2) * Const::EARTH_CIRCUMFERENCE / Const::WORLD_SIZE));
        }
        if (_u_resolution >= 0) {
            glUniform2f(_u_resolution, static_cast<float>(viewState.getWidth()), static_cast<float>(viewState.getHeight()));
        }
        if (_u_starIntensity >= 0) {
            glUniform1f(_u_starIntensity, fog.starIntensity);
        }

        // Start the quad at the horizon plus a margin for the fog band - everything below is drawn
        // over anyway (docs/internals/rendering/08-lighting-sky-fog.md). This is also what bounds
        // the atmosphere's cost: the raymarch runs per FRAGMENT, so the pixels the quad does not
        // cover are the cheapest optimisation available. Not applied when the terrain path draws
        // the sky although the flat horizon says it is not visible.
        float quadBottom = -1.0f;
        if (viewState.isSkyVisible() && isHorizonClipEnabled()) {
            quadBottom = std::max(-1.0f, viewState.getSkyHorizonNDC() - SKY_HORIZON_MARGIN);
        }
        const float quadCoords[8] = { -1, quadBottom, 1, quadBottom, -1, 1, 1, 1 };

        glDisable(GL_CULL_FACE);
        glEnableVertexAttribArray(_a_coord);
        glVertexAttribPointer(_a_coord, 2, GL_FLOAT, GL_FALSE, 0, quadCoords);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(_a_coord);
        glEnable(GL_CULL_FACE);

        GLContext::CheckGLError("SkyRenderer::onDrawFrame");
        return true;
    }

    const float SkyRenderer::QUAD_COORDS[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    const float SkyRenderer::SKY_HORIZON_MARGIN = 0.35f;

    const std::string SkyRenderer::SKY_VERTEX_SHADER = R"GLSL(
        #version 100
        attribute vec2 a_coord;
        uniform mat4 u_invMVPMat;
        varying vec3 v_rayDir;
        void main() {
            // The modelview matrix is relative to the eye, so unprojecting a near-plane point
            // gives the world-space view ray directly.
            vec4 nearPos = u_invMVPMat * vec4(a_coord, -1.0, 1.0);
            v_rayDir = nearPos.xyz / nearPos.w;
            gl_Position = vec4(a_coord, 1.0, 1.0);
        }
    )GLSL";

    const std::string SkyRenderer::SKY_FRAGMENT_SHADER_PREFIX = R"GLSL(
        #version 100
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        precision highp float;
        #else
        precision mediump float;
        #endif
        varying vec3 v_rayDir;
        uniform vec3 u_sunDir;
        uniform vec4 u_sunColor;
        uniform vec4 u_skyColor;
        uniform vec4 u_horizonColor;
        uniform vec4 u_groundColor;
        uniform float u_horizonBlend;
        uniform float u_sunIntensity;
        uniform float u_sunDisc;
        uniform vec4 u_atmosphere;      // scattering sun intensity, exposure luminance
        uniform vec4 u_atmosphereColor; // Rayleigh tint, a = strength
        uniform vec4 u_haloColor;       // Mie tint, a = strength
        uniform float u_time;
        uniform float u_zoom;
        uniform float u_cameraHeight;
        uniform vec2 u_resolution;
        uniform float u_starIntensity;
    )GLSL";

    // Everything both sky types share, and everything a custom skyColor() may call. Comes after
    // the fog block, because the ground wedge and the star fade both read the horizon term from it.
    const std::string SkyRenderer::SKY_FRAGMENT_SHADER_COMMON = R"GLSL(
        // Mapbox's high-color / space-color over the elevation angle: the fog band fades into the
        // upper atmosphere, and that fades into space at the zenith. The high colour uses their
        // exponential rather than a smoothstep - a smoothstep measured from the horizon barely
        // rises within the few degrees of sky a tilted map camera shows, which is why the two
        // colours used to be invisible unless you looked almost straight up. Both are transparent
        // by default and this is then a no-op.
        vec3 atmosphereTint(vec3 color, float elevation) {
            float fadeout = mix(0.0005, 0.25, clamp(uFogParams.w, 0.0, 1.0));
            float high = 1.0 - exp(-(elevation / 3.14159265) / fadeout);
            color = mix(color, uFogHighColor.rgb, uFogHighColor.a * high);
            return mix(color, uFogSpaceColor.rgb, uFogSpaceColor.a * smoothstep(0.35, 1.0, elevation / 1.5707963));
        }

        float starHash(vec2 p) {
            return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
        }

        // Cells in (azimuth, elevation), one star per cell at most, placed at a random point INSIDE
        // its cell and drawn as a soft dot. Lighting the whole cell instead reads as a grid of grey
        // squares, and laying the cells out in a flat projection stretches them into streaks near
        // the horizon.
        float starAmount(vec3 rayDir, float elevation) {
            if (u_starIntensity <= 0.0 || elevation < 0.0) {
                return 0.0;
            }
            vec2 sc = vec2(atan(rayDir.y, rayDir.x), elevation) * 320.0;
            vec2 cell = floor(sc);
            float pick = starHash(cell);
            vec2 pos = vec2(starHash(cell + 1.7), starHash(cell + 5.3));
            float d = length(fract(sc) - pos);
            // ~1.8% of the cells carry one, each with its own brightness.
            float star = step(0.982, pick) * smoothstep(0.34, 0.02, d) * (0.4 + 0.6 * fract(pick * 37.0));
            // At intensity 1 a star has to beat the sky it sits on, hence the gain.
            return star * smoothstep(0.0, 0.10, rayDir.z) * u_starIntensity * 1.7;
        }

        // The disc and the glow around it, shared by both sky types.
        vec4 sunDisc(vec4 color, vec3 rayDir) {
            if (u_sunDisc <= 0.5) {
                return color;
            }
            // Chord length between the two unit vectors, which is the angle in radians to within
            // 1% over the few degrees that matter here - and unlike acos/pow it keeps full
            // precision right at the centre of the disc.
            float d = length(rayDir - u_sunDir);
            float disc = 1.0 - smoothstep(0.0040, 0.0050, d);   // the sun is about 0.5 degrees across
            float glow = exp(-d * d / 0.0012) * 0.45;
            float halo = exp(-d * d / 0.0220) * 0.12;
            // The glow tints towards the sun colour instead of adding to it: an additive glow
            // saturates a bright sky to white long before it reaches the sun.
            color.rgb = mix(color.rgb, u_sunColor.rgb, clamp((halo + glow) * u_sunIntensity, 0.0, 1.0));
            color.rgb += u_sunColor.rgb * u_sunIntensity * disc;
            color.a = max(color.a, disc * u_sunColor.a);
            return color;
        }

        // Below the mathematical horizon, which is exactly the band the drawn ground stops short of:
        // the terrain ends at the view distance, well before the horizon, and everything between the
        // two is this ray. Returning the ground colour alone - transparent by default - left the
        // map's clear colour there, so the hazed ground met it along a hard line. Anything down
        // there is beyond the last tile, so it is haze, and the haze supplies the coverage the
        // ground colour has none of.
        vec4 groundBelowHorizon(vec3 rayDir) {
            return vec4(u_groundColor.rgb, mix(u_groundColor.a, 1.0, uFogColor.a * fogHorizonBlend(rayDir)));
        }
    )GLSL";

    // SkyType::GRADIENT - what the SDK drew before the atmosphere: a horizon-to-zenith gradient
    // with the atmosphere colours over it and a sun disc. The fog is NOT mixed in here; main()
    // applies it once, through skyFog, so a custom fog shader reaches the sky too.
    const std::string SkyRenderer::SKY_FRAGMENT_SHADER_GRADIENT = R"GLSL(
        vec4 skyColor(vec3 rayDir) {
            float elevation = asin(clamp(rayDir.z, -1.0, 1.0));
            if (elevation < 0.0) {
                return groundBelowHorizon(rayDir);
            }
            float t = u_horizonBlend > 0.0 ? clamp(elevation / u_horizonBlend, 0.0, 1.0) : 1.0;
            vec4 color = mix(u_horizonColor, u_skyColor, t);
            color.rgb = atmosphereTint(color.rgb, elevation);
            return sunDisc(color, rayDir);
        }
    )GLSL";

    // Rayleigh and Mie single scattering, integrated along the view ray. Written from the public
    // domain glsl-atmosphere model (wwwtyro, Unlicense - the one maplibre vendors) and Bruneton's
    // "Precomputed Atmospheric Scattering" section 2.1, which is where the coefficients come from.
    // ATMO_STEPS / ATMO_LIGHT_STEPS are defined by the caller so both loops unroll.
    const std::string SkyRenderer::SKY_FRAGMENT_SHADER_SCATTERING = R"GLSL(
        const float PLANET_RADIUS = 6360000.0;
        const float ATMOSPHERE_RADIUS = 6420000.0;
        const vec3 BETA_RAYLEIGH = vec3(5.5e-6, 13.0e-6, 22.4e-6);
        const float BETA_MIE = 21.0e-6;
        const float SCALE_HEIGHT_RAYLEIGH = 8000.0;
        const float SCALE_HEIGHT_MIE = 1200.0;
        const float MIE_G = 0.76;

        // Where a ray leaves a sphere centred on the origin. dir is a unit vector, so the quadratic
        // loses its leading term; the max() keeps a grazing ray from producing a NaN.
        float raySphereExit(vec3 origin, vec3 dir, float radius) {
            float b = dot(dir, origin);
            float c = dot(origin, origin) - radius * radius;
            return -b + sqrt(max(0.0, b * b - c));
        }

        vec2 localDensity(vec3 point) {
            float height = max(length(point) - PLANET_RADIUS, 0.0);
            // Two statements rather than exp(vec2): that form is reported to misbehave on some Mali
            // parts, and this fork ships to them.
            float densityR = exp(-height / SCALE_HEIGHT_RAYLEIGH);
            float densityM = exp(-height / SCALE_HEIGHT_MIE);
            return vec2(densityR, densityM);
        }

        // Optical depth from a point to the top of the atmosphere, towards the sun.
        vec2 densityToSun(vec3 point, vec3 sunDir) {
            float stepLen = raySphereExit(point, sunDir, ATMOSPHERE_RADIUS) / float(ATMO_LIGHT_STEPS);
            vec2 density = vec2(0.0);
            for (int i = 0; i < ATMO_LIGHT_STEPS; i++) {
                density += localDensity(point + sunDir * ((float(i) + 0.5) * stepLen)) * stepLen;
            }
            return density;
        }

        vec3 atmosphere(vec3 rayDir, vec3 sunDir) {
            // The camera stands on the planet, z up, so its height is the only thing that moves the
            // origin - which is what thins the sky seen from a summit at no extra cost.
            vec3 origin = vec3(0.0, 0.0, PLANET_RADIUS + max(0.0, u_cameraHeight));
            float stepLen = raySphereExit(origin, rayDir, ATMOSPHERE_RADIUS) / float(ATMO_STEPS);

            vec3 betaR = BETA_RAYLEIGH * u_atmosphereColor.rgb * u_atmosphereColor.a;
            vec3 betaM = vec3(BETA_MIE) * u_haloColor.rgb * u_haloColor.a;

            vec2 densitySeen = vec2(0.0);
            vec3 scatterR = vec3(0.0);
            vec3 scatterM = vec3(0.0);
            for (int i = 0; i < ATMO_STEPS; i++) {
                vec3 point = origin + rayDir * ((float(i) + 0.5) * stepLen);
                vec2 density = localDensity(point) * stepLen;
                densitySeen += density;
                vec2 total = densitySeen + densityToSun(point, sunDir);
                vec3 attenuation = exp(-(betaR * total.x + betaM * total.y));
                scatterR += density.x * attenuation;
                scatterM += density.y * attenuation;
            }

            // How much of a collision goes towards the eye, per mechanism.
            float mu = dot(rayDir, sunDir);
            float gg = MIE_G * MIE_G;
            float phaseR = 0.0596831 * (1.0 + mu * mu);                          // 3 / (16 pi)
            float phaseM = 0.1193662 * ((1.0 - gg) * (1.0 + mu * mu)) /
                           ((2.0 + gg) * pow(1.0 + gg - 2.0 * MIE_G * mu, 1.5)); // 3 / (8 pi)
            return (scatterR * phaseR * betaR + scatterM * phaseM * betaM) * u_atmosphere.x;
        }

        // Uncharted 2 filmic curve (Hable), normalised at its own white point: the scattered
        // radiance runs well past 1 around the sun, and a plain clamp turns that into a white patch
        // with a hard edge.
        vec3 tonemap(vec3 x) {
            const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
            return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
        }
    )GLSL";

    const std::string SkyRenderer::SKY_FRAGMENT_SHADER_ATMOSPHERE = R"GLSL(
        vec4 skyColor(vec3 rayDir) {
            float elevation = asin(clamp(rayDir.z, -1.0, 1.0));
            if (elevation < 0.0) {
                return groundBelowHorizon(rayDir);
            }
            vec3 scattered = atmosphere(rayDir, u_sunDir);
            vec3 rgb = tonemap(scattered * (8.0 / u_atmosphere.y)) / tonemap(vec3(11.2));
            vec4 color = vec4(rgb, 1.0);
            color.rgb = atmosphereTint(color.rgb, elevation);
            return sunDisc(color, rayDir);
        }
    )GLSL";

    const std::string SkyRenderer::SKY_FRAGMENT_SHADER_MAIN = R"GLSL(
        void main() {
            vec3 rayDir = normalize(v_rayDir);
            vec4 color = clamp(skyColor(rayDir), 0.0, 1.0);
            // The sky is at infinity, so what varies over it is the ANGULAR haze alone - the same
            // term the ground takes, which is what makes the two meet without a seam.
            vec4 premul = skyFog(vec4(color.rgb * color.a, color.a), rayDir);
            // Stars are added AFTER the haze and take only its square root. Added before it, they
            // were multiplied by (1 - haze) like everything else and so were wiped out wherever the
            // fog band reached. They sit beyond the atmosphere: they should dim into it, not be
            // erased by it. This is also why they are here and not in skyColor - a custom sky
            // shader gets them too, and StarIntensity defaults to 0 anyway.
            lowp float haze = uFogColor.a * fogHorizonBlend(rayDir);
            premul.rgb += vec3(starAmount(rayDir, asin(clamp(rayDir.z, -1.0, 1.0)))) * sqrt(1.0 - haze) * premul.a;
            gl_FragColor = premul;
        }
    )GLSL";
}
