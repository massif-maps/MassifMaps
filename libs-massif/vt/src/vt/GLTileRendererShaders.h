/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_GLTILERENDERERSHADERS_H_
#define _MASSIF_VT_GLTILERENDERERSHADERS_H_

namespace massif::vt {
    enum : int {
        A_VERTEXPOSITION,
        A_VERTEXUV,
        A_VERTEXNORMAL,
        A_VERTEXBINORMAL,
        A_VERTEXSKIRT,
        A_VERTEXHEIGHT,
        A_VERTEXBASE,
        A_VERTEXCHORD,
                A_VERTEXCOLOR,
        A_VERTEXATTRIBS,
        A_VERTEXOFFSET
    };

    enum : int {
        U_MVPMATRIX,
        U_TRANSFORMMATRIX,
        U_TILEMATRIX,
        U_UVMATRIX,
        U_BINORMALSCALE,
        U_BINORMALUNITSCALE,
        U_ANTIALIASSCALE,
        U_TILEUNITSCALE,
        U_TILEUNITOFFSET,
        U_UVSCALE,
        U_HEIGHTSCALE,
        U_BASESCALE,
        U_FLOATINGBASE,
        U_SPANDRAPETEXTURE,
        U_SPANDRAPETRANSFORM,
        U_SPANDRAPELIGHT,
        U_GROUNDDRAPETEXTURE,
        U_GROUNDDRAPETRANSFORM,
        U_GROUNDDRAPE,
        U_SPANGROUNDTOLERANCE,
        U_SHADOWHEIGHTSCALE,
        U_EMISSIVE,
        U_COLORTABLE,
        U_WIDTHTABLE,
        U_OFFSETTABLE,
        U_GAPWIDTHTABLE,
        U_BLURTABLE,
        U_STROKEWIDTHTABLE,
        U_STROKESCALETABLE,
        U_PATTERNTABLE,
        U_COLOR,
        U_OPACITY,
        U_PATTERN,
        U_BITMAP,
        U_TEXTURE,
        U_SDFSCALE,
        U_SDFRAMP,
        U_DEPTHBIAS,
        U_DEPTHBIASCLIP,
        U_ELEVATIONTEXTURE,
        U_ELEVATIONUV,
        U_ELEVATIONDECODE,
        U_ELEVATIONOFFSET,
        U_ELEVATIONSCALE,
        U_ELEVATIONTEXELSIZE,
        U_ELEVATIONLATTICECELL,
        U_ELEVATIONNODETEXTURE,
        U_ELEVATIONNODEUV,
        U_TERRAINSPHEREORIGIN,
        U_TERRAINSPHERESCALE,
        U_TERRAINSPHERENODEUV,
        U_TERRAINSPHERETILEUV,
        U_DRAPEBAKE,
        U_ELEVATIONNODETEXELSIZE,
        U_TERRAINEDGECOARSENING,
        U_LAYERDEPTHOFFSET,
        U_DEPTHSHIFT,
        U_DEPTHCLEARANCE,
        U_DRAPETEXTURE,
        U_SUNDIR,
        U_SUNCOLOR,
        U_AMBIENTCOLOR,
        U_LIGHTPARAMS,
        U_GROUNDAOPARAMS,
        U_TERRAINSLOPESCALE,
        U_ELEVATIONGRADIENT,
        U_LIGHTINGFRAME,
        U_TERRAINSPHEREELEVUV,
        U_SHADOWMATRIX,
        U_SHADOWTEXTURE,
        U_SHADOWPARAMS,
        U_SHADOWBIAS,
        U_SHADOWDEPTHSCALE,
        U_SHADOWFADERANGE,
        U_SHADOWNORMALOFFSET,
        U_SHADOWSUNDIR,
        U_SHADOWMASK,
        U_SHADOWMASKSCALE,
        U_FOGCOLOR,
        U_FOGHIGHCOLOR,
        U_FOGSPACECOLOR,
        U_FOGPARAMS,
        U_FOGVERTICAL,
        U_FOGRAY,
        U_DRAPEUVTRANSFORM,
        U_SCREENSCALE,
        U_LABELAXISX,
        U_LABELAXISY,
        U_PAINTSLOPESCALE,
        U_PAINTPARAMS,
        U_GROUNDCOLOR,
        U_LABELOCCLUSIONTEX,
        U_LABELOCCLUSIONPARAMS,
        U_DRAPEMASKTEXTURE,
        U_DRAPEMASKUVTRANSFORM
    };

    enum : unsigned int {
        TRANSFORM_FLAG   = 1,
        OFFSET_FLAG      = 2,
        PATTERN_FLAG     = 4,
        DERIVATIVES_FLAG = 8,
        TERRAIN_FLAG     = 16,
        TERRAIN_VTF_FLAG = 32,
        DRAPE_FLAG       = 64,
        TERRAIN_LIGHT_FLAG = 128,
        TERRAIN_SHADOW_FLAG = 256,
        PAINT_SURFACE_FLAG = 1024,
        FOG_FLAG = 512,
        GROUND_BASE_FLAG = 2048,
        DEM_HW_FILTER_FLAG = 4096,
        // Shadow cascade count (none = one). Compile-time: the matrices and highp varyings per
        // vertex, not the PCF taps, are what a shadowed surface costs.
        SHADOW_CASCADES2_FLAG = 8192,
        SHADOW_CASCADES3_FLAG = 16384,
        SHADOW_CASCADES4_FLAG = 32768,
        // Terrain surface shadow computed once per pixel into a half-res mask (OUT), sampled by
        // every surface covering that pixel (IN): the lookup is a shadowed fragment's main cost.
        SHADOW_MASK_OUT_FLAG = 65536,
        SHADOW_MASK_IN_FLAG = 131072,
        // One tap instead of the kernel, for extrusions: a wall's silhouette is what the eye reads,
        // so the kernel buys far less there than on terrain.
        SHADOW_SINGLE_TAP_FLAG = 262144,
        // Sample the shadow depth buffer directly instead of a packed-RGB copy (ES3-class only).
        SHADOW_DEPTH_TEXTURE_FLAG = 524288,
        // Compile as GLSL ES 3.00; keyword differences are handled by a prelude in createShaderProgram.
        ESSL3_FLAG = 1048576,
        // Hardware depth comparison (sampler2DShadow). Implies ESSL3: GL_EXT_shadow_samplers is not
        // advertised on an ES3 context, where the feature is core.
        SHADOW_HW_FLAG = 2097152,
        // Terrain sun on undraped 2D geometry. Not TERRAIN_LIGHT: the surface shaders declare the
        // same uniforms themselves, and one name twice in a stage is a link error.
        GEOMETRY_LIGHT_FLAG = 4194304,
        // Fade a label whose anchor is behind a 3D occluder in the screen depth texture (mapbox's model).
        LABEL_OCCLUSION_FLAG = 8388608,
        // Write coverage (alpha, replicated) instead of colour, to build a no-drape layer's
        // occlusion mask (docs/internals/rendering/04-terrain.md).
        COVERAGE_FLAG = 16777216,
        // Line carries a CPU-resolved chord height per vertex (bridge/tunnel deck); see LineElevationMode.
        SPAN_FLAG = 33554432,
        // Counterpart of COVERAGE: a no-drape layer scales its alpha by 1 - mask, so draped layers
        // later in style order win where they paint.
        DRAPE_MASK_FLAG = 1073741824,
        // mapbox's `line-gap-width`: one rule draws both strips of a road casing.
        GAPWIDTH_FLAG = 67108864,
        // mapbox's `line-blur`: a widened antialias ramp.
        BLUR_FLAG = 134217728,
        // A bridge deck samples its own span drape (bakeSpanDrapeTile), so its road lands on the
        // deck rather than on the valley floor.
        SPAN_DRAPE_FLAG = 268435456,
        // Receiver is an extrusion: it fights acne with the normal offset, the ground with the
        // receiver-plane bias, and each hurts the other.
        SHADOW_RECEIVER_3D_FLAG = 536870912,
        // Terrain on a sphere: displace along the normal, and tile-local xy is curved so the DEM
        // lookup cannot use it. See docs/internals/rendering/18-globe.md.
        TERRAIN_SPHERICAL_FLAG = 2147483648u
    };

    static const std::map<std::string, int> attribMap = {
        { "aVertexPosition", A_VERTEXPOSITION },
        { "aVertexUV",       A_VERTEXUV },
        { "aVertexNormal",   A_VERTEXNORMAL },
        { "aVertexBinormal", A_VERTEXBINORMAL },
        { "aVertexSkirt",    A_VERTEXSKIRT },
        { "aVertexHeight",   A_VERTEXHEIGHT },
        { "aVertexBase",     A_VERTEXBASE },
        { "aVertexChord",    A_VERTEXCHORD },
        { "aVertexColor",    A_VERTEXCOLOR },
        { "aVertexAttribs",  A_VERTEXATTRIBS },
        { "aVertexOffset",   A_VERTEXOFFSET }
    };

    static const std::map<std::string, int> uniformMap = {
        { "uMVPMatrix",        U_MVPMATRIX },
        { "uTransformMatrix",  U_TRANSFORMMATRIX },
        { "uTileMatrix",       U_TILEMATRIX },
        { "uUVMatrix",         U_UVMATRIX },
        { "uBinormalScale",    U_BINORMALSCALE },
        { "uBinormalUnitScale", U_BINORMALUNITSCALE },
        { "uAntialiasScale",   U_ANTIALIASSCALE },
        { "uTileUnitScale",    U_TILEUNITSCALE },
        { "uTileUnitOffset",   U_TILEUNITOFFSET },
        { "uUVScale",          U_UVSCALE },
        { "uHeightScale",      U_HEIGHTSCALE },
        { "uBaseScale",        U_BASESCALE },
        { "u_emissive",        U_EMISSIVE },
        { "uSpanDrapeTexture", U_SPANDRAPETEXTURE },
        { "uSpanDrapeTransform", U_SPANDRAPETRANSFORM },
        { "uSpanDrapeLight", U_SPANDRAPELIGHT },
        { "uGroundDrapeTexture", U_GROUNDDRAPETEXTURE },
        { "uGroundDrapeTransform", U_GROUNDDRAPETRANSFORM },
        { "uGroundDrape", U_GROUNDDRAPE },
        { "uSpanGroundTolerance", U_SPANGROUNDTOLERANCE },
        { "uFloatingBase",     U_FLOATINGBASE },
        { "uShadowHeightScale", U_SHADOWHEIGHTSCALE },
        { "uColorTable",       U_COLORTABLE },
        { "uWidthTable",       U_WIDTHTABLE },
        { "uOffsetTable",      U_OFFSETTABLE },
        { "uGapWidthTable",    U_GAPWIDTHTABLE },
        { "uBlurTable",        U_BLURTABLE },
        { "uStrokeWidthTable", U_STROKEWIDTHTABLE },
        { "uStrokeScaleTable", U_STROKESCALETABLE },
        { "uPatternTable",     U_PATTERNTABLE },
        { "uPattern",          U_PATTERN },
        { "uBitmap",           U_BITMAP },
        { "uTexture",          U_TEXTURE },
        { "uColor",            U_COLOR },
        { "uOpacity",          U_OPACITY },
        { "uSDFScale",         U_SDFSCALE },
        { "uSDFRamp",          U_SDFRAMP },
        { "uDepthBias",        U_DEPTHBIAS },
        { "uDepthBiasClip",    U_DEPTHBIASCLIP },
        { "uElevationTexture", U_ELEVATIONTEXTURE },
        { "uElevationUV",      U_ELEVATIONUV },
        { "uElevationDecode",  U_ELEVATIONDECODE },
        { "uElevationOffset",  U_ELEVATIONOFFSET },
        { "uElevationScale",   U_ELEVATIONSCALE },
        { "uElevationTexelSize", U_ELEVATIONTEXELSIZE },
        { "uElevationLatticeCell", U_ELEVATIONLATTICECELL },
        { "uElevationNodeTexture", U_ELEVATIONNODETEXTURE },
        { "uElevationNodeUV",      U_ELEVATIONNODEUV },
        { "uTerrainSphereOrigin",  U_TERRAINSPHEREORIGIN },
        { "uTerrainSphereScale",   U_TERRAINSPHERESCALE },
        { "uTerrainSphereNodeUV",  U_TERRAINSPHERENODEUV },
        { "uTerrainSphereTileUV",  U_TERRAINSPHERETILEUV },
        { "uDrapeBake",            U_DRAPEBAKE },
        { "uElevationNodeTexelSize", U_ELEVATIONNODETEXELSIZE },
        { "uTerrainEdgeCoarsening", U_TERRAINEDGECOARSENING },
        { "uLayerDepthOffset",  U_LAYERDEPTHOFFSET },
        { "uDepthShift",        U_DEPTHSHIFT },
        { "uDepthClearance",    U_DEPTHCLEARANCE },
        { "uDrapeTexture",      U_DRAPETEXTURE },
        { "uSunDir",            U_SUNDIR },
        { "uSunColor",          U_SUNCOLOR },
        { "uAmbientColor",      U_AMBIENTCOLOR },
        { "uLightParams",       U_LIGHTPARAMS },
        { "uGroundAOParams",    U_GROUNDAOPARAMS },
        { "uTerrainSlopeScale", U_TERRAINSLOPESCALE },
        { "uElevationGradient", U_ELEVATIONGRADIENT },
        { "uLightingFrame",     U_LIGHTINGFRAME },
        { "uTerrainSphereElevUV", U_TERRAINSPHEREELEVUV },
        { "uShadowMatrix",      U_SHADOWMATRIX },
        { "uShadowTexture",     U_SHADOWTEXTURE },
        { "uShadowParams",      U_SHADOWPARAMS },
        { "uShadowBias",        U_SHADOWBIAS },
        { "uShadowDepthScale",  U_SHADOWDEPTHSCALE },
        { "uShadowFadeRange",   U_SHADOWFADERANGE },
        { "uShadowNormalOffset", U_SHADOWNORMALOFFSET },
        { "uShadowSunDir",      U_SHADOWSUNDIR },
        { "uShadowMask",        U_SHADOWMASK },
        { "uShadowMaskScale",   U_SHADOWMASKSCALE },
        { "uFogColor",          U_FOGCOLOR },
        { "uFogHighColor",      U_FOGHIGHCOLOR },
        { "uFogSpaceColor",     U_FOGSPACECOLOR },
        { "uFogParams",         U_FOGPARAMS },
        { "uFogVertical",       U_FOGVERTICAL },
        { "uFogRay",            U_FOGRAY },
        { "uDrapeUVTransform",  U_DRAPEUVTRANSFORM },
        { "uScreenScale",       U_SCREENSCALE },
        { "uLabelAxisX",        U_LABELAXISX },
        { "uLabelAxisY",        U_LABELAXISY },
        { "uPaintSlopeScale",   U_PAINTSLOPESCALE },
        { "uPaintParams",       U_PAINTPARAMS },
        { "uGroundColor",       U_GROUNDCOLOR },
        { "uLabelOcclusionTex",    U_LABELOCCLUSIONTEX },
        { "uLabelOcclusionParams", U_LABELOCCLUSIONPARAMS },
        { "uDrapeMask",            U_DRAPEMASKTEXTURE },
        { "uDrapeMaskUVTransform", U_DRAPEMASKUVTRANSFORM }
    };

    static const std::map<unsigned int, std::string> flagDefineMap = {
        { TRANSFORM_FLAG,   "TRANSFORM" },
        { OFFSET_FLAG,      "OFFSET" },
        { GAPWIDTH_FLAG,    "GAPWIDTH" },
        { BLUR_FLAG,        "BLUR" },
        { SHADOW_RECEIVER_3D_FLAG, "SHADOW_RECEIVER_3D" },
        { PATTERN_FLAG,     "PATTERN" },
        { DERIVATIVES_FLAG, "DERIVATIVES" },
        { TERRAIN_FLAG,     "TERRAIN_DEPTH_BIAS" },
        { TERRAIN_VTF_FLAG, "TERRAIN" },
        { DRAPE_FLAG,       "DRAPE" },
        { TERRAIN_LIGHT_FLAG, "TERRAIN_LIGHT" },
        { TERRAIN_SHADOW_FLAG, "TERRAIN_SHADOW" },
        { PAINT_SURFACE_FLAG, "PAINT_SURFACE" },
        { FOG_FLAG, "FOG" },
        { GROUND_BASE_FLAG, "GROUND_BASE" },
        { DEM_HW_FILTER_FLAG, "DEM_HW_FILTER" },
        { SHADOW_CASCADES2_FLAG, "SHADOW_CASCADES_2" },
        { SHADOW_CASCADES3_FLAG, "SHADOW_CASCADES_3" },
        { SHADOW_CASCADES4_FLAG, "SHADOW_CASCADES_4" },
        { LABEL_OCCLUSION_FLAG, "LABEL_OCCLUSION" },
        { SHADOW_MASK_OUT_FLAG, "SHADOW_MASK_OUT" },
        { SHADOW_MASK_IN_FLAG, "SHADOW_MASK_IN" },
        { SHADOW_SINGLE_TAP_FLAG, "SHADOW_SINGLE_TAP" },
        { SHADOW_DEPTH_TEXTURE_FLAG, "SHADOW_DEPTH_TEXTURE" },
        { ESSL3_FLAG, "ESSL3" },
        { SHADOW_HW_FLAG, "SHADOW_HW" },
        { GEOMETRY_LIGHT_FLAG, "GEOMETRY_LIGHT" },
        { COVERAGE_FLAG, "COVERAGE" },
        { SPAN_FLAG, "SPAN" },
        { DRAPE_MASK_FLAG, "DRAPE_MASK" },
        { SPAN_DRAPE_FLAG, "SPAN_DRAPE" },
        { TERRAIN_SPHERICAL_FLAG, "TERRAIN_SPHERICAL" }
    };

    static const std::string textureFiltersFsh = R"GLSL(
        float w0(highp_opt float a) {
            return (1.0 / 6.0) * (a * (a * (-a + 3.0) - 3.0) + 1.0);
        }

        float w1(highp_opt float a) {
            return (1.0 / 6.0) * (a * a * (3.0 * a - 6.0) + 4.0);
        }

        float w2(highp_opt float a) {
            return (1.0 / 6.0) * (a * (a * (-3.0 * a + 3.0) + 3.0) + 1.0);
        }

        float w3(highp_opt float a) {
            return (1.0 / 6.0) * (a * a * a);
        }

        float g0(highp_opt float a) {
            return w0(a) + w1(a);
        }

        float g1(highp_opt float a) {
            return w2(a) + w3(a);
        }

        float h0(highp_opt float a) {
            return -1.0 + w1(a) / (w0(a) + w1(a));
        }

        float h1(highp_opt float a) {
            return 1.0 + w3(a) / (w2(a) + w3(a));
        }

        vec4 texture2D_nearest(sampler2D tex, highp_opt vec2 uv0, highp_opt vec4 res) {
            highp_opt vec2 uv = uv0 * res.xy + 0.5;
            highp_opt vec2 iuv = floor(uv);

            highp_opt vec2 p0 = (vec2(iuv.x, iuv.y) - 0.5) * res.zw;
            return texture2D(tex, p0);
        }

        vec4 texture2D_bilinear(sampler2D tex, highp_opt vec2 uv0, highp_opt vec4 res) {
            return texture2D(tex, uv0);
        }

        vec4 texture2D_bicubic(sampler2D tex, highp_opt vec2 uv0, highp_opt vec4 res) {
            highp_opt vec2 uv = uv0 * res.xy + 0.5;
            highp_opt vec2 iuv = floor(uv);
            highp_opt vec2 fuv = fract(uv);

            highp_opt float g0x = g0(fuv.x);
            highp_opt float g1x = g1(fuv.x);
            highp_opt float h0x = h0(fuv.x);
            highp_opt float h1x = h1(fuv.x);
            highp_opt float h0y = h0(fuv.y);
            highp_opt float h1y = h1(fuv.y);
            highp_opt float g0y = g0(fuv.y);
            highp_opt float g1y = g1(fuv.y);

            highp_opt vec2 p0 = (vec2(iuv.x + h0x, iuv.y + h0y) - 0.5) * res.zw;
            highp_opt vec2 p1 = (vec2(iuv.x + h1x, iuv.y + h0y) - 0.5) * res.zw;
            highp_opt vec2 p2 = (vec2(iuv.x + h0x, iuv.y + h1y) - 0.5) * res.zw;
            highp_opt vec2 p3 = (vec2(iuv.x + h1x, iuv.y + h1y) - 0.5) * res.zw;
            return g0y * (g0x * texture2D(tex, p0) + g1x * texture2D(tex, p1)) + g1y * (g0x * texture2D(tex, p2) + g1x * texture2D(tex, p3));
        }
    )GLSL";

    static const std::string commonVsh = R"GLSL(
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        #define highp_opt highp
        #else
        #define highp_opt mediump
        #endif
        #ifdef TERRAIN_SPHERICAL
        // World -> view east/north/up: lighting shaders (apps' too) assume z is up and uSunDir.z is
        // the sun's height, but a globe normal is the sphere's (18-globe.md).
        uniform mediump mat3 uLightingFrame;
        mediump vec3 lightingNormal(mediump vec3 normal) {
            return normal * uLightingFrame;
        }
        #else
        mediump vec3 lightingNormal(mediump vec3 normal) {
            return normal;
        }
        #endif
        #ifdef TERRAIN_DEPTH_BIAS
        uniform float uDepthBias;     // NDC-constant component (scaled by w)
        uniform float uDepthBiasClip; // clip-constant component: tracks mesh interpolation error (tangram depth_shift)
        uniform float uLayerDepthOffset; // painter-order model: (proxy - layer). 0 in slack mode
        uniform float uDepthShift;       // painter-order near-camera separation boost
        uniform float uDepthClearance;   // metre-constant clearance: proj[2][3] * metres, what a draped line needs
        vec4 applyDepthBias(vec4 clipPos) {
            float z = clipPos.z
                + uLayerDepthOffset * (0.0000019073486328125 * clipPos.w + uDepthShift)
                + uDepthClearance / clipPos.w
                - (uDepthBias * clipPos.w + uDepthBiasClip);
            return vec4(clipPos.xy, z, clipPos.w);
        }
        #else
        vec4 applyDepthBias(vec4 clipPos) {
            return clipPos;
        }
        #endif
        #if defined(TERRAIN_SHADOW) && defined(SHADOW_MASK_IN)
        // The mask is sampled by screen position, so nothing has to be carried per vertex.
        void applyShadowPos(highp vec3 pos, mediump vec3 normal) {
        }
        void applyShadowPos(highp vec3 pos) {
        }
        #elif defined(TERRAIN_SHADOW)
        #if defined(SHADOW_CASCADES_4)
        #define SHADOW_CASCADES 4
        #elif defined(SHADOW_CASCADES_3)
        #define SHADOW_CASCADES 3
        #elif defined(SHADOW_CASCADES_2)
        #define SHADOW_CASCADES 2
        #else
        #define SHADOW_CASCADES 1
        #endif
        // Tile-local -> light clip, per cascade and per tile so input stays in [0,1]. All cascades
        // are computed: only the fragment stage knows which one it needs.
        uniform highp mat4 uShadowMatrix[SHADOW_CASCADES];
        // mapbox's normal offset: move the lookup along the normal rather than biasing depth, so the
        // shadow stays attached to the wall casting it.
        uniform mediump vec4 uShadowNormalOffset;
        uniform mediump vec3 uShadowSunDir;
        varying highp vec3 vShadowPos0;
        #if SHADOW_CASCADES >= 2
        varying highp vec3 vShadowPos1;
        #endif
        #if SHADOW_CASCADES >= 3
        varying highp vec3 vShadowPos2;
        #endif
        #if SHADOW_CASCADES >= 4
        varying highp vec3 vShadowPos3;
        #endif
        void applyShadowPos(highp vec3 pos, mediump vec3 normal) {
            // Grazing faces need the full texel, sun-facing ones almost none (mapbox's curve).
            mediump float dotScale = min(1.0 - dot(normal, uShadowSunDir), 1.0) * 0.5 + 0.5;
            mediump vec3 offset = normal * dotScale;
            highp vec4 clip0 = uShadowMatrix[0] * vec4(pos + offset * uShadowNormalOffset.x, 1.0);
            vShadowPos0 = clip0.xyz / clip0.w * 0.5 + 0.5;
        #if SHADOW_CASCADES >= 2
            highp vec4 clip1 = uShadowMatrix[1] * vec4(pos + offset * uShadowNormalOffset.y, 1.0);
            vShadowPos1 = clip1.xyz / clip1.w * 0.5 + 0.5;
        #endif
        #if SHADOW_CASCADES >= 3
            highp vec4 clip2 = uShadowMatrix[2] * vec4(pos + offset * uShadowNormalOffset.z, 1.0);
            vShadowPos2 = clip2.xyz / clip2.w * 0.5 + 0.5;
        #endif
        #if SHADOW_CASCADES >= 4
            highp vec4 clip3 = uShadowMatrix[3] * vec4(pos + offset * uShadowNormalOffset.w, 1.0);
            vShadowPos3 = clip3.xyz / clip3.w * 0.5 + 0.5;
        #endif
        }
        // Terrain and draped content take N.L from the DEM per fragment: no vertex normal, no offset.
        void applyShadowPos(highp vec3 pos) {
            applyShadowPos(pos, vec3(0.0));
        }
        #else
        void applyShadowPos(highp vec3 pos, mediump vec3 normal) {
        }
        void applyShadowPos(highp vec3 pos) {
        }
        #endif
        // Vertex frame units -> tile units (1 for the surface). Declared in every mode: the flat line
        // path uses it too.
        uniform highp vec2 uTileUnitScale;
        uniform highp vec2 uTileUnitOffset;
        #ifdef TERRAIN_SPHERICAL
        // Unit-sphere point = origin + pos * scale. Outside the terrain block: a globe curves its
        // geometry with or without a DEM (18-globe.md).
        uniform highp vec3 uTerrainSphereOrigin;
        uniform highp vec3 uTerrainSphereScale;
        // Target tile's unit square from Mercator radians: unit = (merc - xy) * zw.
        uniform highp vec4 uTerrainSphereTileUV;
        // 1 while baking the drape: the target is the tile's unit square, not the world.
        uniform highp float uDrapeBake;
        // Inverse of SphericalTileTransformer::tileToSpherical.
        highp vec3 terrainSpherePoint(vec3 pos) {
            return uTerrainSphereOrigin + pos * uTerrainSphereScale;
        }
        // As SphericalProjectionSurface::SphericalToInternal, in radians (WORLD_SIZE / 2pi is in the uvs).
        highp vec2 terrainSphereToMercator(highp vec3 p) {
            highp float len = length(p);
            highp float rz = clamp(p.z / len, -0.999999, 0.999999);
            return vec2(atan(p.y, p.x), 0.5 * log((1.0 + rz) / (1.0 - rz)));
        }
        // Relative to the frame origin, never forming o + d: a tile is 1e-5 of the sphere, below
        // fp32 precision of the absolute point (18-globe.md).
        highp vec2 terrainSphereMercatorDelta(highp vec3 pos) {
            highp vec3 o = uTerrainSphereOrigin;    // the frame origin, ON the unit sphere
            highp vec3 d = pos * uTerrainSphereScale;
            highp float od = dot(o, d), dd = dot(d, d);
            highp float len = sqrt(1.0 + 2.0 * od + dd);
            highp float lenM1 = (2.0 * od + dd) / (len + 1.0); // |o + d| - 1, without the cancellation
            highp float dz = (d.z - o.z * lenM1) / len;        // sin(lat) difference, small by construction
            // atanh(a) - atanh(b) = atanh((a - b) / (1 - a b)); the series keeps small x exact.
            highp float x = dz / max(1.0e-6, 1.0 - o.z * (o.z + dz));
            highp float dMercY = abs(x) < 0.01 ? x * (1.0 + x * x * 0.33333333) : 0.5 * log((1.0 + x) / (1.0 - x));
            // cross and dot of o and o + d, expanded in d alone
            highp float dLon = atan(o.x * d.y - o.y * d.x, o.x * (o.x + d.x) + o.y * (o.y + d.y));
            return vec2(dLon, dMercY);
        }
        // Longitude wrap: a coarse stand-in frame can sit up to a world away, and the offset is
        // small either way (18-globe.md).
        highp vec2 terrainSphereRelative(highp vec3 pos, highp vec2 origin) {
            highp vec2 merc = terrainSphereMercatorDelta(pos) - origin;
            merc.x -= 6.283185307179586 * floor(merc.x * 0.15915494309189535 + 0.5);
            return merc;
        }
        highp vec2 terrainSphereTileUnit(highp vec3 pos) {
            return terrainSphereRelative(pos, uTerrainSphereTileUV.xy) * uTerrainSphereTileUV.zw;
        }
        // The bake target is the tile's unit square, so a vertex is placed by its tile position.
        highp vec4 drapeBakeClip(highp mat4 mvp, highp vec3 pos) {
            return mvp * vec4(terrainSphereTileUnit(pos), 0.0, 1.0);
        }
        #endif
        #ifdef TERRAIN
        uniform highp sampler2D uElevationTexture;
        uniform highp vec4 uElevationUV;     // elevation texture uv = uv.xy + pos.xy * uv.zw
        uniform vec4 uElevationDecode;       // meters = dot(texture sample, decode) + offset
        uniform float uElevationOffset;      // constant term: a 2-channel texture has no free channel for it
        uniform highp vec4 uElevationScale;  // x: meters to vertex z units (equator), y/z: mercator y = y + pos.y * z, w: vertex frame z offset
        uniform highp vec4 uElevationTexelSize; // xy: texture size in texels, zw: 1 / size
        uniform highp vec2 uElevationLatticeCell; // regular-grid surface cell size in NODE-uv units (0 = off = plain node sample)
        #ifdef TERRAIN_SPHERICAL
        // Drop below the surface: 0 except on a skirt's bottom ring.
        attribute highp float aVertexSkirt;
        // Node uv from internal Mercator: uv = (internal - xy) * zw; tile-local xy is curved here.
        uniform highp vec4 uTerrainSphereNodeUV;
        // Same, for the full elevation texture.
        uniform highp vec4 uTerrainSphereElevUV;
        #endif
        uniform highp vec4 uTerrainEdgeCoarsening; // lattice cell scale (2^k, 1 = off) on the west/east/south/north tile edge
        // DEM box-filtered to one texel per mesh node: point-sampling the full DEM aliases relief
        // finer than a cell. The full texture is for the fragment stage.
        uniform highp sampler2D uElevationNodeTexture;
        uniform highp vec4 uElevationNodeUV;        // node texture uv = uv.xy + pos.xy * uv.zw
        uniform highp vec4 uElevationNodeTexelSize; // xy: texture size in texels, zw: 1 / size

        float sampleNode(highp vec2 uv) {
            return dot(texture2D(uElevationNodeTexture, uv), uElevationDecode) + uElevationOffset;
        }
        // Manual bilinear: several mobile GPUs filter vertex-stage fetches as NEAREST. DEM_HW_FILTER
        // (one hardware fetch, as tangram) is a measurement switch, not a default.
        #ifdef DEM_HW_FILTER
        float nodeMeters(highp vec2 uv) {
            return sampleNode(uv);
        }
        #else
        float nodeMeters(highp vec2 uv) {
            highp vec2 texelPos = uv * uElevationNodeTexelSize.xy - 0.5;
            highp vec2 texelBase = floor(texelPos);
            highp vec2 f = texelPos - texelBase;
            highp vec2 uv00 = (texelBase + 0.5) * uElevationNodeTexelSize.zw;
            float h00 = sampleNode(uv00);
            float h10 = sampleNode(uv00 + vec2(uElevationNodeTexelSize.z, 0.0));
            float h01 = sampleNode(uv00 + vec2(0.0, uElevationNodeTexelSize.w));
            float h11 = sampleNode(uv00 + uElevationNodeTexelSize.zw);
            return mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
        }
        #endif
        #ifdef TERRAIN_SPHERICAL
        highp vec2 terrainSphereElevUV(highp vec3 pos) {
            return terrainSphereRelative(pos, uTerrainSphereElevUV.xy) * uTerrainSphereElevUV.zw;
        }
        #endif
        vec3 applyTerrain(vec3 pos) {
        #ifdef TERRAIN_SPHERICAL
            highp vec2 uv = terrainSphereRelative(pos, uTerrainSphereNodeUV.xy) * uTerrainSphereNodeUV.zw;
        #else
            highp vec2 uv = uElevationNodeUV.xy + pos.xy * uElevationNodeUV.zw;
        #endif
            float meters;
            if (uElevationLatticeCell.x != 0.0) {
                // Same two-triangle split as the surface mesh (anti-diagonal), so draped geometry
                // follows it between nodes. On an edge, the coarser neighbour's cell reproduces its
                // chords; the edge test is in tile units.
                highp vec2 unitPos = pos.xy * uTileUnitScale;
                highp vec2 cell = uElevationLatticeCell;
                if (unitPos.x < 0.00001) cell.y *= uTerrainEdgeCoarsening.x;       // west edge
                else if (unitPos.x > 0.99999) cell.y *= uTerrainEdgeCoarsening.y;  // east edge
                if (unitPos.y < 0.00001) cell.x *= uTerrainEdgeCoarsening.z;       // south edge
                else if (unitPos.y > 0.99999) cell.x *= uTerrainEdgeCoarsening.w;  // north edge
                highp vec2 rel = (uv - uElevationNodeUV.xy) / cell;
                highp vec2 gi = floor(rel);
                highp vec2 fg = rel - gi;
                highp vec2 uv00 = uElevationNodeUV.xy + gi * cell;
                float H00 = nodeMeters(uv00);
                float H10 = nodeMeters(uv00 + vec2(cell.x, 0.0));
                float H01 = nodeMeters(uv00 + vec2(0.0, cell.y));
                float H11 = nodeMeters(uv00 + cell);
                if (fg.x + fg.y <= 1.0) {
                    meters = H00 + (H10 - H00) * fg.x + (H01 - H00) * fg.y;
                } else {
                    meters = H10 * (1.0 - fg.y) + H01 * (1.0 - fg.x) + H11 * (fg.x + fg.y - 1.0);
                }
            } else {
                meters = nodeMeters(uv);
            }
            // Mercator stretch; on a sphere y/z are 0 so cosh is 1 (heights are radial).
            highp float my = uElevationScale.y + pos.y * uElevationScale.z;
            float coshMY = 0.5 * (exp(my) + exp(-my));
            float z = meters * uElevationScale.x * coshMY + uElevationScale.w;
        #ifdef TERRAIN_SPHERICAL
            // Skirt drop in its own attribute: the flat encoding below replaces pos.z, which would
            // lose the sphere point.
            return pos + normalize(terrainSpherePoint(pos)) * (z - aVertexSkirt);
        #else
            if (pos.z < -900000.0) {
                // skirt bottom vertex: z encodes -1000000 - drop, hiding cracks between LODs
                z += pos.z + 1000000.0;
            }
            return vec3(pos.xy, z);
        #endif
        }
        #else
        vec3 applyTerrain(vec3 pos) {
            return pos;
        }
        #endif
        // 2D content lit by the terrain takes the surface's normal. The surface shaders declare
        // these themselves under TERRAIN_LIGHT.
        #if defined(TERRAIN) && (defined(TERRAIN_SHADOW) || defined(GEOMETRY_LIGHT)) && !defined(TERRAIN_LIGHT)
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;
        #ifdef TERRAIN_SPHERICAL
        varying highp vec3 vSphereUp;
        #endif
        void setTerrainSlopeVaryings(highp vec3 pos) {
        #ifdef TERRAIN_SPHERICAL
            vElevUV = terrainSphereElevUV(pos);
            vSphereUp = normalize(terrainSpherePoint(pos));
            highp float sphereMY = terrainSphereToMercator(terrainSpherePoint(pos)).y;
            vElevCosh = 0.5 * (exp(sphereMY) + exp(-sphereMY));
        #else
            vElevUV = uElevationUV.xy + pos.xy * uElevationUV.zw;
            highp float slopeMY = uElevationScale.y + pos.y * uElevationScale.z;
            vElevCosh = 0.5 * (exp(slopeMY) + exp(-slopeMY));
        #endif
        }
        // A deck must not take the valley's normal: zero stretch makes terrainNdl() see a flat one.
        void setSpanFlatShading() {
            vElevCosh = 0.0;
        }
        #else
        void setTerrainSlopeVaryings(highp vec3 pos) {
        }
        void setSpanFlatShading() {
        }
        #endif
    )GLSL";

    // Verbatim copy of FogShader::HELPERS (all/native), the master; any difference is a bug.
    static const std::string fogHelpersFsh = R"GLSL(
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

        lowp float fogVertical(highp float heightM) {
            return uFogVertical.y > uFogVertical.x ? smoothstep(uFogVertical.x, uFogVertical.y, heightM) : 0.0;
        }
    )GLSL";

    // Verbatim copy of FogShader::BUILTIN; replaced by an app's own blend. Premultiplied, so alpha is untouched.
    static const std::string fogBlendFsh = R"GLSL(
        lowp vec4 applyFog(lowp vec4 color, highp vec3 dir, highp float dist, highp float heightM) {
            lowp float amount = fogOpacity(fogRange(dist)) * fogHorizonBlend(dir);
            amount *= 1.0 - fogVertical(heightM);
            return vec4(mix(color.rgb, uFogColor.rgb * color.a, amount), color.a);
        }

        lowp vec4 skyFog(lowp vec4 color, highp vec3 dir) {
            lowp float amount = uFogColor.a * fogHorizonBlend(dir);
            return vec4(mix(color.rgb, uFogColor.rgb * color.a, amount), color.a);
        }

        lowp float fogLabelFade() {
            highp vec3 rayVec = fogRayVec();
            highp float dist = length(rayVec) / max(1.0e-9, gl_FragCoord.w) * uFogParams.z;
            return 1.0 - smoothstep(0.9, 1.0, fogOpacity(fogRange(dist)));
        }
    )GLSL";

    // The blend must come after the helpers it calls and before applyFog(color).
    static const std::string FOG_HELPERS_PLACEHOLDER = "$FOG_HELPERS$";
    static const std::string FOG_BLEND_PLACEHOLDER = "$FOG_BLEND$";

    static const std::string commonFsh = R"GLSL(
        #if defined(DERIVATIVES) && !defined(ESSL3)
        #extension GL_OES_standard_derivatives : enable
        #endif
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        #define highp_opt highp
        #else
        #define highp_opt mediump
        #endif

        precision mediump float;
        #ifdef FOG
        uniform lowp vec4 uFogColor;      // rgb = fog colour, a = how opaque the fog gets at full distance
        uniform lowp vec4 uFogHighColor;  // the upper atmosphere, for a custom fog shader
        uniform lowp vec4 uFogSpaceColor; // the zenith, for a custom fog shader
        uniform highp vec4 uFogParams;    // range start, 1 / (end - start), internal -> range units, horizon blend
        uniform highp vec4 uFogVertical;  // fade-out start and end in metres, metres per internal unit, camera height in metres
        uniform highp mat3 uFogRay;       // view ray basis, see FogShader::rayBasis

        $FOG_HELPERS$

        // Built-in blends or FogOptions::setShaderSource: one block covers tiles, background and sky.
        $FOG_BLEND$

        // No varying needed (fogRayVec). The ortho drape bake has w = 1 and never fogs; the surface
        // it is painted on fogs it later.
        lowp vec4 applyFog(lowp vec4 color) {
            highp vec3 rayVec = fogRayVec();
            highp float rayLen = length(rayVec);
            highp vec3 dir = rayVec / rayLen;
            highp float dist = rayLen / max(1.0e-9, gl_FragCoord.w);
            highp float heightM = uFogVertical.w + dist * dir.z * uFogVertical.z;
            return applyFog(color, dir, dist * uFogParams.z, heightM);
        }
        #else
        lowp vec4 applyFog(lowp vec4 color) {
            return color;
        }
        lowp float fogLabelFade() {
            return 1.0;
        }
        #endif
        // A DEM normal is in local east/north/up, not uSunDir's frame on a globe (18-globe.md).
        #if defined(TERRAIN) && defined(TERRAIN_SPHERICAL) && (defined(TERRAIN_SHADOW) || defined(GEOMETRY_LIGHT) || defined(TERRAIN_LIGHT))
        uniform mediump mat3 uLightingFrame;
        varying highp vec3 vSphereUp;
        mediump vec3 groundLightNormal(mediump vec3 n) {
            highp vec3 up = normalize(vSphereUp);
            highp float h = max(1.0e-6, length(up.xy));
            highp vec3 east = vec3(-up.y, up.x, 0.0) / h;
            highp vec3 north = vec3(-up.z * up.x, -up.z * up.y, h) / h;
            return (east * n.x + north * n.y + up * n.z) * uLightingFrame;
        }
        #else
        mediump vec3 groundLightNormal(mediump vec3 n) {
            return n;
        }
        #endif
        // Same stencil as the surface, so a road and its ground get the same N.L, bias and back-face rule.
        #if defined(TERRAIN) && (defined(TERRAIN_SHADOW) || defined(GEOMETRY_LIGHT)) && !defined(TERRAIN_LIGHT)
        uniform highp sampler2D uElevationTexture;
        uniform highp vec4 uElevationDecode; // 'vec4' in the vertex stage means highp there
        uniform highp vec4 uElevationTexelSize;
        uniform mediump vec3 uSunDir;          // east, north, up
        uniform highp vec2 uTerrainSlopeScale; // metres of height -> world units, per elevation-uv unit
        uniform highp sampler2D uElevationGradient;
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;

        mediump float terrainNdl() {
            highp vec2 duv = uElevationTexelSize.zw;
            // Forward differences sit on texel edges: half a texel back, linear filtering is tangram's quadratic stencil.
            highp vec2 grad = vec2(texture2D(uElevationGradient, vElevUV - vec2(0.5 * duv.x, 0.0)).r,
                                   texture2D(uElevationGradient, vElevUV - vec2(0.0, 0.5 * duv.y)).g); // metres per texel
            highp float dx = grad.x * uTerrainSlopeScale.x * vElevCosh / duv.x;
            highp float dy = grad.y * uTerrainSlopeScale.y * vElevCosh / duv.y;
            return max(0.0, dot(groundLightNormal(normalize(vec3(-dx, -dy, 1.0))), uSunDir));
        }
        #else
        mediump float terrainNdl() {
            return 1.0;
        }
        #endif
        #if defined(TERRAIN_SHADOW) && defined(SHADOW_MASK_IN)
        uniform sampler2D uShadowMask;
        uniform highp vec2 uShadowMaskScale; // 1 / screen size, whatever resolution the mask is at
        mediump float shadowFactorScreen() {
            return texture2D(uShadowMask, gl_FragCoord.xy * uShadowMaskScale).r;
        }
        mediump float shadowFactorSlope(mediump float ndl) {
            return shadowFactorScreen();
        }
        mediump float shadowFactorSlopeParts(mediump float ndl, out mediump float mapLit) {
            mapLit = shadowFactorScreen();
            return mapLit;
        }
        mediump float shadowFactor() {
            return shadowFactorScreen();
        }
        #elif defined(TERRAIN_SHADOW)
        #if defined(SHADOW_CASCADES_4)
        #define SHADOW_CASCADES 4
        #elif defined(SHADOW_CASCADES_3)
        #define SHADOW_CASCADES 3
        #elif defined(SHADOW_CASCADES_2)
        #define SHADOW_CASCADES 2
        #else
        #define SHADOW_CASCADES 1
        #endif
        #ifdef SHADOW_HW
        // One fetch = four depth compares, bilinearly averaged in the texture unit.
        uniform highp sampler2DShadow uShadowTexture;
        #else
        uniform sampler2D uShadowTexture;
        #endif
        uniform mediump vec4 uShadowParams; // x = 1/mapSize within one cascade, y = strength, z = PCF radius in texels, w = 1/cascade count
        // mapbox's u_shadow_bias shape (x constant, y slope growth, z cap) but in metres: our light
        // box spans hundreds of km. uShadowDepthScale converts per cascade.
        uniform mediump vec3 uShadowBias;
        uniform highp vec4 uShadowDepthScale; // 1 / depth range in metres, per cascade
        // Last cascade fade as view depth (mapbox's [far * 0.75, far]); zero = no fade, for the drape bake.
        uniform highp vec2 uShadowFadeRange;
        varying highp vec3 vShadowPos0;
        #if SHADOW_CASCADES >= 2
        varying highp vec3 vShadowPos1;
        #endif
        #if SHADOW_CASCADES >= 3
        varying highp vec3 vShadowPos2;
        #endif
        #if SHADOW_CASCADES >= 4
        varying highp vec3 vShadowPos3;
        #endif

        bool outsideShadowPage(highp vec3 pos, mediump float margin) {
            return pos.x < margin || pos.x > 1.0 - margin || pos.y < margin || pos.y > 1.0 - margin || pos.z < 0.0 || pos.z > 1.0;
        }

        // uv in atlas space: cascades are pages of one texture, near page first. 1 = lit.
        mediump float shadowTap(highp vec2 uv, highp float ref) {
        #if defined(SHADOW_HW)
            return texture(uShadowTexture, vec3(uv, ref));
        #elif defined(SHADOW_DEPTH_TEXTURE)
            return ref <= texture2D(uShadowTexture, uv).r ? 1.0 : 0.0;
        #else
            highp vec4 enc = texture2D(uShadowTexture, uv);
            return ref <= dot(enc.rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)) ? 1.0 : 0.0;
        #endif
        }
        // mapLit is the shadow map's part only (1 where not consulted): a receiver dimming its
        // ambient must use it, or its own back faces lose the sky too.
        mediump float shadowFactorSlopeParts(mediump float ndl, out mediump float mapLit) {
            mapLit = 1.0;
            // Sharpest page the fragment falls in; the margin keeps PCF taps off the neighbouring page.
            mediump float margin = uShadowParams.x * (uShadowParams.z + 1.0);
            highp vec3 pos = vShadowPos0;
            mediump float page = 0.0;
        #if SHADOW_CASCADES >= 2
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos1;
                page = 1.0;
            }
        #endif
        #if SHADOW_CASCADES >= 3
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos2;
                page = 2.0;
            }
        #endif
        #if SHADOW_CASCADES >= 4
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos3;
                page = 3.0;
            }
        #endif
            highp float o = uShadowParams.x * uShadowParams.z;
            highp float ref = pos.z;
            mediump float facing = smoothstep(0.0, 0.15, ndl);
            // Receiver-plane slope: stored depth is the texel centre's, metres off at a grazing sun.
            // Ground only (extrusion bevels are discontinuous); before any early return so quad
            // derivatives stay defined.
            highp vec2 dzduv = vec2(0.0);
        #if defined(DERIVATIVES) && !defined(SHADOW_RECEIVER_3D)
            {
                highp vec3 dpdx = dFdx(pos);
                highp vec3 dpdy = dFdy(pos);
                highp float det = dpdx.x * dpdy.y - dpdx.y * dpdy.x;
                if (abs(det) > 1.0e-12) {
                    dzduv.x = ( dpdy.y * dpdx.z - dpdx.y * dpdy.z) / det;
                    dzduv.y = (-dpdy.x * dpdx.z + dpdx.x * dpdy.z) / det;
                    // Cap as a fraction of box depth per texel: a metric cap vanishes as the box grows.
                    highp float limit = 0.02 / max(1.0e-6, uShadowParams.x);
                    dzduv = clamp(dzduv, vec2(-limit), vec2(limit));
                }
            }
        #endif
            if (pos.x < 0.0 || pos.x > 1.0 || pos.y < 0.0 || pos.y > 1.0 || pos.z < 0.0 || pos.z > 1.0) {
                // Unshadowed, but keep the back-face rule or a ring shows where the last cascade ends.
                return mix(1.0, facing, uShadowParams.y);
            }
            if (facing <= 0.0) {
                return mix(1.0, 0.0, uShadowParams.y);
            }
            // mapbox's capped slope-scaled bias; tan(acos(x)) written out, the direct form NaNs in mediump.
            mediump float ndlBias = clamp(ndl, 0.05, 1.0);
            mediump float slope = sqrt(1.0 - ndlBias * ndlBias) / ndlBias;
            highp float depthScale = uShadowDepthScale.x;
        #if SHADOW_CASCADES >= 2
            if (page > 0.5) { depthScale = uShadowDepthScale.y; }
        #endif
        #if SHADOW_CASCADES >= 3
            if (page > 1.5) { depthScale = uShadowDepthScale.z; }
        #endif
        #if SHADOW_CASCADES >= 4
            if (page > 2.5) { depthScale = uShadowDepthScale.w; }
        #endif
            ref -= (0.5 * uShadowBias.x + clamp(uShadowBias.y * slope, 0.0, uShadowBias.z)) * depthScale;
            // Rise over the texels a compare reads: half a texel, a whole one under hardware PCF's 2x2.
        #ifdef SHADOW_HW
            ref -= uShadowParams.x * (abs(dzduv.x) + abs(dzduv.y));
        #else
            ref -= 0.5 * uShadowParams.x * (abs(dzduv.x) + abs(dzduv.y));
        #endif
            // Offsets stay in page space so the kernel is square in the map.
            highp vec2 atlasScale = vec2(uShadowParams.w, 1.0);
            highp vec2 atlasBase = vec2(page * uShadowParams.w, 0.0);
            mediump float lit = 0.0;
        #ifdef SHADOW_SINGLE_TAP
            lit = shadowTap(atlasBase + pos.xy * atlasScale, ref);
        #else
            // Four diagonal taps instead of 3x3: nearly the same answer, same penumbra width.
            highp float d = o * 0.75;
            for (int j = 0; j < 2; j++) {
                for (int i = 0; i < 2; i++) {
                    highp vec2 offset = vec2(float(i) * 2.0 - 1.0, float(j) * 2.0 - 1.0) * d;
                    // Each tap on the receiver's own plane, or the uphill taps read the ground above it.
                    lit += shadowTap(atlasBase + (pos.xy + offset) * atlasScale, ref + dot(dzduv, offset));
                }
            }
            lit *= 0.25;
        #endif
            // Fade the last cascade over view depth (mapbox): a uv-edge fade projects to a hard line.
            mediump float lastPage = 1.0 / uShadowParams.w - 1.0;
            if (page >= lastPage - 0.5 && uShadowFadeRange.y > 0.0) {
                // 1 / gl_FragCoord.w = clip w, what mapbox carries in a varying.
                highp float viewDepth = 1.0 / max(1.0e-9, gl_FragCoord.w);
                lit = mix(lit, 1.0, smoothstep(uShadowFadeRange.x, uShadowFadeRange.y, viewDepth));
            }
            // Back faces are shadowed outright (the map sees them edge-on), which lets the bias stay small.
            mapLit = mix(1.0, lit, uShadowParams.y);
            lit = min(lit, facing);
            return mix(1.0, lit, uShadowParams.y);
        }
        mediump float shadowFactorSlope(mediump float ndl) {
            mediump float mapLit;
            return shadowFactorSlopeParts(ndl, mapLit);
        }
        mediump float shadowFactor() {
            return shadowFactorSlope(1.0);
        }
        #endif
        // Undraped 2D content on the ground takes the ground's sun and shadow, to match draped neighbours.
        #if defined(TERRAIN) && defined(GEOMETRY_LIGHT) && !defined(TERRAIN_LIGHT)
        uniform lowp vec4 uSunColor;        // rgb = colour, a = unused
        uniform lowp vec4 uAmbientColor;    // rgb = colour, a = unused
        uniform mediump vec2 uLightParams;  // x = sun intensity, y = ambient intensity
        lowp vec4 applyTerrainShading(lowp vec4 color) {
            mediump float ndl = terrainNdl();
            // Same Lambert as backgroundFsh; premultiplied, so the clamp keeps rgb <= a.
            mediump vec3 lit = uAmbientColor.rgb * uLightParams.y + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
        #if defined(TERRAIN_SHADOW) && defined(SHADOW_MASK_IN)
            lit *= shadowFactorScreen();
        #elif defined(TERRAIN_SHADOW)
            lit *= shadowFactorSlope(ndl);
        #endif
            return vec4(min(color.rgb * lit, vec3(color.a)), color.a);
        }
        #elif defined(TERRAIN_SHADOW) && defined(SHADOW_MASK_IN)
        lowp vec4 applyTerrainShading(lowp vec4 color) {
            return vec4(color.rgb * shadowFactorScreen(), color.a);
        }
        #elif defined(TERRAIN_SHADOW)
        lowp vec4 applyTerrainShading(lowp vec4 color) {
            return vec4(color.rgb * shadowFactorSlope(terrainNdl()), color.a);
        }
        #else
        lowp vec4 applyTerrainShading(lowp vec4 color) {
            return color;
        }
        #endif
        // The drape is drawn before live geometry; masking by the coverage of later draped layers
        // puts a no-drape layer back in its style position.
        #ifdef DRAPE_MASK
        uniform lowp sampler2D uDrapeMask;
        uniform highp vec4 uDrapeMaskUVTransform; // target-tile units -> mask-tile units
        lowp vec4 applyDrapeMask(lowp vec4 color, mediump vec2 tileUnit) {
            return color * (1.0 - texture2D(uDrapeMask, uDrapeMaskUVTransform.xy + tileUnit * uDrapeMaskUVTransform.zw).r);
        }
        #else
        lowp vec4 applyDrapeMask(lowp vec4 color, mediump vec2 tileUnit) {
            return color;
        }
        #endif
    )GLSL";

    // Not in commonVsh: the paint path declares the same varyings.
    static const std::string terrainLightVsh = R"GLSL(
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;
        #ifdef TERRAIN_SPHERICAL
        varying highp vec3 vSphereUp;
        #endif
        void setTerrainLightVaryings(highp vec3 pos) {
        #ifdef TERRAIN_SPHERICAL
            vElevUV = terrainSphereElevUV(pos);
            vSphereUp = normalize(terrainSpherePoint(pos));
            highp float sphereMY = terrainSphereToMercator(terrainSpherePoint(pos)).y;
            vElevCosh = 0.5 * (exp(sphereMY) + exp(-sphereMY));
        #else
            vElevUV = uElevationUV.xy + pos.xy * uElevationUV.zw;
            highp float lightMY = uElevationScale.y + pos.y * uElevationScale.z;
            vElevCosh = 0.5 * (exp(lightMY) + exp(-lightMY));
        #endif
        }
        #else
        void setTerrainLightVaryings(highp vec3 pos) {
        }
        #endif
    )GLSL";

    static const std::string backgroundVsh = terrainLightVsh + R"GLSL(
        attribute vec3 aVertexPosition;
        #if defined(LIGHTING_FSH) || defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
        uniform mat4 uMVPMatrix;
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        uniform vec2 uUVScale;
        varying highp_opt vec2 vUV;
        #endif
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif
        #ifdef LIGHTING_VSH
        varying lowp vec4 vColor;
        #endif
        #ifdef DRAPE
        // xy = uv offset, zw = uv scale; a sub-rect when standing in on an ancestor's texture.
        uniform highp vec4 uDrapeUVTransform;
        varying highp_opt vec2 vDrapeUV;
        #endif

        void main(void) {
        #ifdef PATTERN
            vUV = aVertexUV * uUVScale;
        #endif
            setTerrainLightVaryings(aVertexPosition);
        #ifdef DRAPE
            // Surface vertex xy is the tile-local [0,1] uv the drape was baked with.
        #ifdef TERRAIN_SPHERICAL
            vDrapeUV = uDrapeUVTransform.xy + terrainSphereTileUnit(aVertexPosition) * uDrapeUVTransform.zw;
        #else
            vDrapeUV = uDrapeUVTransform.xy + aVertexPosition.xy * uDrapeUVTransform.zw;
        #endif
        #endif
        #ifdef LIGHTING_VSH
            vColor = applyLighting(vec4(1.0, 1.0, 1.0, 1.0), lightingNormal(aVertexNormal));
        #endif
        #ifdef LIGHTING_FSH
            vNormal = lightingNormal(aVertexNormal);
        #endif
            highp vec3 terrainPos = applyTerrain(aVertexPosition);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        }
    )GLSL";

    // Terrain caster: depth packed into RGB (no depth-texture extension). Uses backgroundVsh with the
    // light matrix, so caster geometry is bit-identical to the drawn one.
    static const std::string shadowCasterFsh = R"GLSL(
        void main(void) {
            highp float depth = gl_FragCoord.z;
            highp vec3 enc = vec3(1.0, 255.0, 65025.0) * depth; // 'packed' is a reserved word
            enc = fract(enc);
            enc -= enc.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
            glFragColor = vec4(enc, 1.0);
        }
    )GLSL";

    // Same half-open tile clip as the drawn extrusion: under overzoom a buffer-margin copy from the
    // neighbour tile would otherwise shadow the drawn roof.
    static const std::string polygon3DShadowCasterFsh = R"GLSL(
        varying highp_opt vec2 vTilePos;

        void main(void) {
            if (vTilePos.x < 0.0 || vTilePos.x >= 1.0 || vTilePos.y < 0.0 || vTilePos.y >= 1.0) {
                discard;
            }
            highp float depth = gl_FragCoord.z;
            highp vec3 enc = vec3(1.0, 255.0, 65025.0) * depth;
            enc = fract(enc);
            enc -= enc.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
            glFragColor = vec4(enc, 1.0);
        }
    )GLSL";

    // Not in commonFsh: the terrain-paint path declares the same names, and a duplicate does not compile.
    static const std::string terrainLightFsh = R"GLSL(
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
        // Precision must match the vertex-stage declarations, or GLSL ES 1.00 fails to link.
        uniform highp sampler2D uElevationTexture;
        uniform highp vec4 uElevationDecode; // 'vec4' in the vertex stage means highp there
        uniform highp vec4 uElevationTexelSize;
        uniform mediump vec3 uSunDir;         // east, north, up
        uniform lowp vec4 uSunColor;          // rgb = colour, a = unused
        uniform lowp vec4 uAmbientColor;      // rgb = colour, a = unused
        uniform mediump vec2 uLightParams;    // x = sun intensity, y = ambient intensity
        uniform highp vec2 uTerrainSlopeScale; // metres of height -> world units, per elevation-uv unit
        uniform highp sampler2D uElevationGradient;
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;

        mediump vec3 terrainNormal() {
            highp vec2 duv = uElevationTexelSize.zw;
            // Forward differences sit on texel edges: half a texel back, linear filtering is tangram's quadratic stencil.
            highp vec2 grad = vec2(texture2D(uElevationGradient, vElevUV - vec2(0.5 * duv.x, 0.0)).r,
                                   texture2D(uElevationGradient, vElevUV - vec2(0.0, 0.5 * duv.y)).g); // metres per texel
            highp float dx = grad.x * uTerrainSlopeScale.x * vElevCosh / duv.x;
            highp float dy = grad.y * uTerrainSlopeScale.y * vElevCosh / duv.y;
            return groundLightNormal(normalize(vec3(-dx, -dy, 1.0)));
        }
        #endif
    )GLSL";

    static const std::string backgroundFsh = terrainLightFsh + R"GLSL(
        uniform lowp vec4 uColor;
        uniform lowp float uOpacity;
        #ifdef PATTERN
        uniform sampler2D uPattern;
        varying highp_opt vec2 vUV;
        #endif
        #ifdef DRAPE
        uniform sampler2D uDrapeTexture;
        varying highp_opt vec2 vDrapeUV;
        #endif
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif
        #ifdef LIGHTING_VSH
        varying lowp vec4 vColor;
        #endif

        void main(void) {
        #ifdef DRAPE
            lowp vec4 color = texture2D(uDrapeTexture, vDrapeUV);
        #elif defined(PATTERN)
            lowp vec4 patternColor = texture2D(uPattern, vUV);
            lowp vec4 color = uColor * (1.0 - patternColor.a) + patternColor;
        #else
            lowp vec4 color = uColor;
        #endif
        #ifdef COVERAGE
            glFragColor = vec4(color.a * uOpacity);
            return;
        #endif
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
            // Premultiplied: scale rgb, clamp to alpha.
            mediump float ndl = max(0.0, dot(terrainNormal(), uSunDir));
        #if defined(SHADOW_MASK_OUT) && defined(TERRAIN_SHADOW)
            // Mask pass: same geometry and normal as the surface, so it samples back what it would compute.
            glFragColor = vec4(vec3(shadowFactorSlope(ndl)), 1.0);
            return;
        #endif
            // Normalised Lambert: the sun fills the headroom above ambient, so a clipped highlight
            // cannot hide a shadow.
            mediump vec3 lit = uAmbientColor.rgb * uLightParams.y + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
        #ifdef TERRAIN_SHADOW
            // Multiplies the final colour, not N.L, where it vanished at ambient 1.
            lit *= shadowFactorSlope(ndl);
        #endif
            color = vec4(min(color.rgb * lit, vec3(color.a)), color.a);
        #endif
        #if defined(LIGHTING_VSH)
            glFragColor = applyFog(vColor * color * uOpacity);
        #elif defined(LIGHTING_FSH)
            glFragColor = applyFog(applyLighting(color, normalize(vNormal)) * uOpacity);
        #else
            glFragColor = applyFog(color * uOpacity);
        #endif
        }
    )GLSL";

    static const std::string colormapVsh = terrainLightVsh + R"GLSL(
        attribute vec3 aVertexPosition;
        #if defined(LIGHTING_FSH) || defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
        attribute vec2 aVertexUV;
        uniform mat4 uMVPMatrix;
        uniform mat3 uUVMatrix;
        varying highp_opt vec2 vUV;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif
        #ifdef LIGHTING_VSH
        varying lowp vec4 vColor;
        #endif

        void main(void) {
            vUV = vec2(uUVMatrix * vec3(aVertexUV, 1.0));
            setTerrainLightVaryings(aVertexPosition);
        #ifdef LIGHTING_VSH
            vColor = applyLighting(vec4(1.0, 1.0, 1.0, 1.0), lightingNormal(aVertexNormal));
        #endif
        #ifdef LIGHTING_FSH
            vNormal = lightingNormal(aVertexNormal);
        #endif
            highp vec3 terrainPos = applyTerrain(aVertexPosition);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        }
    )GLSL";

    static const std::string colormapFsh = terrainLightFsh + R"GLSL(
        uniform sampler2D uBitmap;
        uniform highp_opt vec4 uUVScale;
        uniform lowp float uOpacity;
        varying highp_opt vec2 vUV;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif
        #ifdef LIGHTING_VSH
        varying lowp vec4 vColor;
        #endif

        void main(void) {
        #if defined(FILTER_NEAREST)
            lowp vec4 color = texture2D_nearest(uBitmap, vUV, uUVScale);
        #elif defined(FILTER_BICUBIC)
            lowp vec4 color = texture2D_bicubic(uBitmap, vUV, uUVScale);
        #else
            lowp vec4 color = texture2D_bilinear(uBitmap, vUV, uUVScale);
        #endif
        #ifdef COVERAGE
            glFragColor = vec4(color.a * uOpacity);
            return;
        #endif
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
            // Premultiplied: scale rgb, clamp to alpha.
            mediump float ndl = max(0.0, dot(terrainNormal(), uSunDir));
            // Normalised Lambert: the sun fills the headroom above ambient, so a clipped highlight
            // cannot hide a shadow.
            mediump vec3 lit = uAmbientColor.rgb * uLightParams.y + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
        #ifdef TERRAIN_SHADOW
            // Multiplies the final colour, not N.L, where it vanished at ambient 1.
            lit *= shadowFactorSlope(ndl);
        #endif
            color = vec4(min(color.rgb * lit, vec3(color.a)), color.a);
        #endif
        #if defined(LIGHTING_VSH)
            glFragColor = applyFog(vColor * color * uOpacity);
        #elif defined(LIGHTING_FSH)
            glFragColor = applyFog(applyLighting(color, normalize(vNormal)) * uOpacity);
        #else
            glFragColor = applyFog(color * uOpacity);
        #endif
        }
    )GLSL";

    static const std::string normalmapVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        attribute vec2 aVertexUV;
        #ifdef LIGHTING_FSH
        attribute vec3 aVertexNormal;
        attribute vec3 aVertexBinormal;
        varying mediump vec3 vNormal;
        varying mediump vec3 vBinormal;
        #endif
        uniform mat4 uMVPMatrix;
        uniform mat3 uUVMatrix;
        varying highp_opt vec2 vUV;

        void main(void) {
            vUV = vec2(uUVMatrix * vec3(aVertexUV, 1.0));
        #ifdef LIGHTING_FSH
            vNormal = lightingNormal(aVertexNormal);
            vBinormal = lightingNormal(aVertexBinormal);
        #endif
            highp vec3 terrainPos = applyTerrain(aVertexPosition);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        }
    )GLSL";

    // Helpers a custom normal-map shader may call: getElevation(), getMapZoom(), sampleElevation(uv).
    static const std::string normalmapCustomPrelude = R"GLSL(
        uniform sampler2D uBitmap;
        uniform highp_opt vec4 uUVScale;
        varying highp_opt vec2 vUV;
        // > 0.5: RG = normal.xy, BA = 16-bit elevation, contrast from a uniform. Else RGB = normal, A = contrast.
        uniform mediump float u_elevationEncoded;
        uniform highp_opt vec2 u_elevationDecode; // meters = elev16 * x + y
        uniform lowp float u_contrast;            // replaces the alpha channel when elevation-encoded
        uniform highp_opt float u_zoom;           // fractional map zoom

        highp_opt float decodeElevation(lowp vec4 s) {
            return (s.b * 255.0 * 256.0 + s.a * 255.0) * u_elevationDecode.x + u_elevationDecode.y;
        }
        // Decode before blending: filtering the hi/lo bytes wraps into false contour lines.
        highp_opt float sampleElevation(highp_opt vec2 uv) {
            highp_opt vec2 tc = uv * uUVScale.xy - 0.5;
            highp_opt vec2 f = fract(tc);
            highp_opt vec2 base = (floor(tc) + 0.5) * uUVScale.zw;
            highp_opt float e00 = decodeElevation(texture2D(uBitmap, base));
            highp_opt float e10 = decodeElevation(texture2D(uBitmap, base + vec2(uUVScale.z, 0.0)));
            highp_opt float e01 = decodeElevation(texture2D(uBitmap, base + vec2(0.0, uUVScale.w)));
            highp_opt float e11 = decodeElevation(texture2D(uBitmap, base + uUVScale.zw));
            return mix(mix(e00, e10, f.x), mix(e01, e11, f.x), f.y);
        }
        highp_opt float getElevation() { return sampleElevation(vUV); }
        highp_opt float getMapZoom() { return u_zoom; }
        // Untouched source texel, e.g. for a CustomRasterTileLayer filter shader.
        lowp vec4 getRawColor() { return texture2D(uBitmap, vUV); }
    )GLSL";

    static const std::string normalmapFsh = R"GLSL(
        uniform lowp float uOpacity;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        varying mediump vec3 vBinormal;
        uniform lowp vec4 u_contourColor;
        uniform highp_opt float u_contourInterval; // meters between contour lines; <= 0 disables the built-in contours
        uniform mediump float u_contourWidth;      // contour half-width in screen pixels
        #endif

        void main(void) {
        #if defined(FILTER_NEAREST)
            lowp vec4 packedNormalAlpha = texture2D_nearest(uBitmap, vUV, uUVScale);
        #elif defined(FILTER_BICUBIC)
            lowp vec4 packedNormalAlpha = texture2D_bicubic(uBitmap, vUV, uUVScale);
        #else
            lowp vec4 packedNormalAlpha = texture2D_bilinear(uBitmap, vUV, uUVScale);
        #endif
            lowp vec4 color = vec4(packedNormalAlpha.a);
        #ifdef COVERAGE
            // A hillshade covers its whole tile whatever the shading resolves to.
            glFragColor = vec4(uOpacity);
            return;
        #endif
        #if defined(LIGHTING_FSH)
            mediump vec3 tspaceNormal;
            if (u_elevationEncoded > 0.5) {
                mediump vec2 nxy = packedNormalAlpha.xy * 2.0 - vec2(1.0);
                tspaceNormal = vec3(nxy, sqrt(max(0.0, 1.0 - dot(nxy, nxy))));
                color = vec4(u_contrast); // alpha holds elevation here
            } else {
                tspaceNormal = packedNormalAlpha.xyz * 2.0 - vec3(1.0, 1.0, 1.0);
            }
            mediump vec3 normal = normalize(vNormal);
            mediump vec3 tangent = normalize(cross(vBinormal, vNormal));
            mediump vec3 binormal = cross(normal, tangent);
            mediump vec3 wspaceNormal = mat3(tangent, binormal, normal) * tspaceNormal;
            mediump float dotp = dot(normal, wspaceNormal);
            mediump float intensity = sqrt(max(0.0, 1.0 - dotp * dotp));
            lowp vec4 shade = applyLighting(color, wspaceNormal, normal, intensity);
            if (u_elevationEncoded > 0.5 && u_contourInterval > 0.0) {
                // Screen-width antialiased contours: metre distance / fwidth (tangram style).
                highp_opt float e = sampleElevation(vUV);
                highp_opt float frac = fract(e / u_contourInterval);
                highp_opt float distM = min(frac, 1.0 - frac) * u_contourInterval;
                mediump float px = distM / max(fwidth(e), 1e-4);
                mediump float cov = clamp(u_contourWidth - px + 0.5, 0.0, 1.0) * u_contourColor.a;
                // premultiplied over
                shade.rgb = u_contourColor.rgb * cov + shade.rgb * (1.0 - cov);
                shade.a = cov + shade.a * (1.0 - cov);
            }
            glFragColor = applyFog(shade * uOpacity);
        #else
            glFragColor = applyFog(color * uOpacity);
        #endif
        }
    )GLSL";

    // Terrain paint: hillshade from the shared terrain DEM, one draw per tile, no normal-map tile set.
    static const std::string terrainPaintPrelude = R"GLSL(
        uniform highp sampler2D uElevationTexture;
        // Precision must match the vertex-stage declarations, or GLSL ES 1.00 fails to link.
        uniform highp vec4 uElevationDecode;
        uniform highp float uElevationOffset;   // the decode's constant term
        uniform highp vec4 uElevationTexelSize; // xy: texture size in texels, zw: 1 / size
        uniform highp vec2 uPaintSlopeScale;    // metres per texel -> dimensionless slope (height scale folded in)
        uniform mediump vec4 uPaintParams;      // x = contrast, y = opacity, zw reserved
        uniform highp_opt float u_zoom;         // fractional map zoom
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;

        highp float sampleElevation(highp vec2 uv) {
            return dot(texture2D(uElevationTexture, uv), uElevationDecode) + uElevationOffset;
        }

        // tangram's hillshade.yaml: 3x3 stencil at texel centres plus a quadratic expansion; a
        // central difference is constant per texel and facets the shading.
        highp float gTerrainElev;      // metres at this fragment
        highp vec2 gTerrainGrad;       // metres per elevation texel, (du, dv), v growing north

        void terrainPaintSample(out highp float elev, out highp vec2 gradPerTexel) {
            highp vec2 duv = uElevationTexelSize.zw;
            highp vec2 ij = vElevUV * uElevationTexelSize.xy;
            highp vec2 cen = floor(ij) + 0.5;
            highp vec2 uv = cen * duv;
            highp float h00 = sampleElevation(uv - duv);
            highp float h01 = sampleElevation(uv + vec2(-duv.x, 0.0));
            highp float h02 = sampleElevation(uv + vec2(-duv.x, duv.y));
            highp float h10 = sampleElevation(uv + vec2(0.0, -duv.y));
            highp float h11 = sampleElevation(uv);
            highp float h12 = sampleElevation(uv + vec2(0.0, duv.y));
            highp float h20 = sampleElevation(uv + vec2(duv.x, -duv.y));
            highp float h21 = sampleElevation(uv + vec2(duv.x, 0.0));
            highp float h22 = sampleElevation(uv + duv);
            highp vec2 f = ij - cen;
            highp float ddxy = (h22 - h20 - h02 + h00) * 0.25;
            highp mat2 curv = mat2(h21 - 2.0 * h11 + h01, ddxy, ddxy, h12 - 2.0 * h11 + h10);
            highp vec2 grad0 = vec2(h21 - h01, h12 - h10) * 0.5;
            gradPerTexel = grad0 + curv * f;
            elev = h11 + dot(f, grad0) + 0.5 * dot(f, curv * f);
        }

        void terrainPaintPrepare() { terrainPaintSample(gTerrainElev, gTerrainGrad); }

        // Same custom-shader contract as the normal-map path.
        highp float getElevation() { return gTerrainElev; }
        highp float getMapZoom() { return u_zoom; }
        lowp vec4 getRawColor() { return texture2D(uElevationTexture, vElevUV); }

        // (dh/dEast, -dh/dNorth) as the normal map encodes it; v grows north in the elevation texture.
        mediump vec2 terrainPaintDeriv() {
            return vec2(gTerrainGrad.x, -gTerrainGrad.y) * vElevCosh * uPaintSlopeScale;
        }
    )GLSL";

    static const std::string terrainPaintVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        uniform mat4 uMVPMatrix;
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;
        #ifdef TERRAIN_SPHERICAL
        varying highp vec3 vSphereUp;
        #endif

        void main(void) {
        #ifdef TERRAIN_SPHERICAL
            vElevUV = terrainSphereElevUV(aVertexPosition);
            highp float sphereMY = terrainSphereToMercator(terrainSpherePoint(aVertexPosition)).y;
            vElevCosh = 0.5 * (exp(sphereMY) + exp(-sphereMY));
            vSphereUp = normalize(terrainSpherePoint(aVertexPosition));
        #else
            vElevUV = uElevationUV.xy + aVertexPosition.xy * uElevationUV.zw;
            highp float my = uElevationScale.y + aVertexPosition.y * uElevationScale.z;
            vElevCosh = 0.5 * (exp(my) + exp(-my));
        #endif
        #ifdef PAINT_SURFACE
            // Drawn as the displaced terrain surface itself (tangram's model).
            highp vec3 terrainPos = applyTerrain(aVertexPosition);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        #else
            // Flat ortho drape bake: the quad is the tile's unit square.
            gl_Position = uMVPMatrix * vec4(aVertexPosition.xy, 0.0, 1.0);
        #endif
        }
    )GLSL";

    static const std::string terrainPaintFsh = R"GLSL(
        #ifdef GROUND_BASE
        // The paint is the ground: it shades the ground colour, no separate fill draw (tangram).
        uniform lowp vec4 uGroundColor;
        #endif
        #ifdef PAINT_SURFACE
        // Same contours as the normal-map path; declared here since normalmapFsh is not linked in.
        uniform lowp vec4 u_contourColor;
        uniform highp_opt float u_contourInterval; // metres between contour lines; <= 0 disables them
        uniform mediump float u_contourWidth;      // contour half-width in screen pixels
        #endif
        #if defined(PAINT_SURFACE) && defined(TERRAIN_LIGHT)
        // Takes the surface's sun and shadow, or it covers them with an unlit copy. Names from the
        // prelude are not redeclared (link error).
        uniform mediump vec3 uSunDir;          // east, north, up
        uniform lowp vec4 uSunColor;           // rgb = colour, a = unused
        uniform lowp vec4 uAmbientColor;       // rgb = colour, a = unused
        uniform mediump vec2 uLightParams;     // x = sun intensity, y = ambient intensity
        uniform highp vec2 uTerrainSlopeScale; // metres of height -> world units, per elevation-uv unit

        // Geometric normal: terrainPaintDeriv() carries the hillshade's relief boost.
        mediump vec3 terrainSurfaceNormal() {
            highp vec2 st = uElevationTexelSize.zw;
            highp float dx = gTerrainGrad.x * uTerrainSlopeScale.x * vElevCosh / st.x;
            highp float dy = gTerrainGrad.y * uTerrainSlopeScale.y * vElevCosh / st.y;
            return groundLightNormal(normalize(vec3(-dx, -dy, 1.0)));
        }
        #endif

        void main(void) {
            terrainPaintPrepare();
            mediump vec2 deriv = terrainPaintDeriv();
            // applyLighting() recovers vec2(-n.x, n.y)/n.z = deriv. Flat surface normal: the terrain tilts it later.
            mediump vec3 normal = normalize(vec3(-deriv.x, deriv.y, 1.0));
            lowp vec4 color = applyLighting(vec4(uPaintParams.x), normal, vec3(0.0, 0.0, 1.0), 0.0);
        #ifdef PAINT_SURFACE
            color = color * uPaintParams.y;
        #if defined(TERRAIN_LIGHT)
            // Same Lambert and shadow multiply as backgroundFsh.
            mediump float ndl = max(0.0, dot(terrainSurfaceNormal(), uSunDir));
            mediump vec3 lit = uAmbientColor.rgb * uLightParams.y + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
        #ifdef TERRAIN_SHADOW
            lit *= shadowFactorSlope(ndl);
        #endif
            color = vec4(min(color.rgb * lit, vec3(color.a)), color.a);
        #endif
            if (u_contourInterval > 0.0) {
                // Same contours as normalmapFsh; the quadratic elevation does not kink at texel edges.
                highp_opt float e = getElevation();
                highp_opt float frac = fract(e / u_contourInterval);
                highp_opt float distM = min(frac, 1.0 - frac) * u_contourInterval;
                mediump float px = distM / max(fwidth(e), 1e-4);
                mediump float cov = clamp(u_contourWidth - px + 0.5, 0.0, 1.0) * u_contourColor.a;
                color.rgb = u_contourColor.rgb * cov + color.rgb * (1.0 - cov);
                color.a = cov + color.a * (1.0 - cov);
            }
        #ifdef GROUND_BASE
            // Premultiplied over the ground colour.
            color = vec4(color.rgb + uGroundColor.rgb * uGroundColor.a * (1.0 - color.a),
                         color.a + uGroundColor.a * (1.0 - color.a));
        #endif
            glFragColor = applyFog(color);
        #else
            glFragColor = color * uPaintParams.y;
        #endif
        }
    )GLSL";

    static const std::string blendVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        uniform mat4 uMVPMatrix;

        void main(void) {
            gl_Position = uMVPMatrix * vec4(aVertexPosition, 1.0);
        }
    )GLSL";

    static const std::string blendFsh = R"GLSL(
        uniform sampler2D uTexture;
        uniform lowp vec4 uColor;
        uniform highp_opt vec2 uUVScale;

        void main(void) {
            lowp vec4 color = texture2D(uTexture, gl_FragCoord.xy * uUVScale);
            glFragColor = color * uColor;
        }
    )GLSL";

    static const std::string labelVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        // Glyph corner relative to the anchor; camera-axis offsets are resolved here so a label batch
        // uploads once, not once per frame.
        attribute vec3 aVertexOffset;
        #if defined(LIGHTING_FSH) || defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
        attribute vec2 aVertexUV;
        attribute vec4 aVertexColor;
        attribute vec4 aVertexAttribs;
        uniform vec3 uLabelAxisX;
        uniform vec3 uLabelAxisY;
        uniform mat4 uMVPMatrix;
        #ifdef LABEL_OCCLUSION
        uniform sampler2D uLabelOcclusionTex;
        // x = half tap square (uv), y = depth offset (NDC), z = occluded opacity, w = 1 / soft ramp
        uniform vec4 uLabelOcclusionParams;
        #endif
        uniform vec2 uUVScale;
        uniform float uSDFRamp;
        uniform vec4 uColorTable[16];
        uniform float uWidthTable[16];
        uniform float uStrokeWidthTable[16];
        varying lowp vec4 vColor;
        // Label plate border colour (mode 3); zero for every other mode.
        varying lowp vec4 vBorderColor;
        varying highp_opt vec2 vUV;
        varying mediump vec4 vAttribs;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float size = uWidthTable[styleIndex];
            float opacity = aVertexAttribs[2] * (1.0 / 127.0);
            bool plate = aVertexAttribs[1] > 2.5;
            vec4 color = (aVertexAttribs[1] > 1.0 && !plate) ? vec4(1.0, 1.0, 1.0, 1.0) : uColorTable[styleIndex];
            // Border is the slot after the fill (LabelPlateIndices). Not +1 otherwise: a driver may
            // evaluate both sides of the select and read past the 16-slot table.
            int borderIndex = plate ? styleIndex + 1 : styleIndex;
            vec4 borderColor = plate ? uColorTable[borderIndex] : vec4(0.0, 0.0, 0.0, 0.0);
            vUV = aVertexUV * uUVScale;
            // [1] = halo width in screen pixels, [3] = one screen pixel of signed distance.
            vAttribs = vec4(aVertexAttribs[1], uStrokeWidthTable[styleIndex], 0.0, uSDFRamp / size);
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, lightingNormal(aVertexNormal)) * opacity;
            vBorderColor = applyLighting(borderColor, lightingNormal(aVertexNormal)) * opacity;
        #else
            vColor = color * opacity;
            vBorderColor = borderColor * opacity;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = lightingNormal(aVertexNormal);
        #endif
            // attribs[3] bit 0: offset on camera axes; bit 1: absolute anchor height.
            vec3 offset = mod(aVertexAttribs[3], 2.0) > 0.5
                ? uLabelAxisX * aVertexOffset.x + uLabelAxisY * aVertexOffset.y
                : aVertexOffset;
            // Height from the surface elevation (as mapbox), except deck labels: a span chord is CPU-only.
            highp vec3 anchorPos = aVertexPosition;
        #ifdef TERRAIN
            if (aVertexAttribs[3] < 1.5) {
                // Whole position: on a globe the lift is radial.
                anchorPos = applyTerrain(aVertexPosition);
            }
        #endif
        #ifdef LABEL_OCCLUSION
            // Per anchor, so a wall never cuts a glyph run in half; four soft taps so no single
            // half-res texel decides.
            highp vec4 anchorClip = uMVPMatrix * vec4(anchorPos, 1.0);
            if (anchorClip.w > 0.0) {
                highp vec2 anchorUV = anchorClip.xy / anchorClip.w * 0.5 + 0.5;
                highp float anchorDepth = anchorClip.z / anchorClip.w * 0.5 + 0.5 + uLabelOcclusionParams.y;
                highp vec2 d = vec2(uLabelOcclusionParams.x);
                // Depth packed as the shadow caster does; an empty white texel decodes past 1.
                highp vec3 unpack = vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0);
                highp vec4 taps = vec4(
                    dot(texture2D(uLabelOcclusionTex, anchorUV + vec2( d.x,  d.y)).rgb, unpack),
                    dot(texture2D(uLabelOcclusionTex, anchorUV + vec2(-d.x,  d.y)).rgb, unpack),
                    dot(texture2D(uLabelOcclusionTex, anchorUV + vec2( d.x, -d.y)).rgb, unpack),
                    dot(texture2D(uLabelOcclusionTex, anchorUV + vec2(-d.x, -d.y)).rgb, unpack));
                lowp float visible = dot(vec4(0.25), clamp((taps - vec4(anchorDepth)) * uLabelOcclusionParams.w, 0.0, 1.0));
                lowp float occlusion = mix(uLabelOcclusionParams.z, 1.0, visible);
                vColor *= occlusion;
                vBorderColor *= occlusion;
            }
        #endif
            gl_Position = uMVPMatrix * vec4(anchorPos + offset, 1.0);
        }
    )GLSL";

    static const std::string labelFsh = R"GLSL(
        uniform sampler2D uBitmap;
        varying lowp vec4 vColor;
        varying lowp vec4 vBorderColor;
        varying highp_opt vec2 vUV;
        varying mediump vec4 vAttribs;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            // mediump: at lowp the field quantizes coarser than the one-pixel ramp (banded edges).
            mediump vec4 color = texture2D(uBitmap, vUV);
            if (vAttribs[0] > 2.5) {
                // Plate: r = fill coverage, a = whole plate. One blend, so a translucent fill does
                // not show the border under it.
                color = vColor * color.r + vBorderColor * (color.a - color.r);
            } else if (vAttribs[0] > 0.0) {
                color = color * vColor;
            } else {
        #ifdef DERIVATIVES
                // One screen pixel in field units; unlike the per-batch value, follows perspective.
                mediump float size = max(length(vec2(dFdx(color.r), dFdy(color.r))), 0.00001);
        #else
                mediump float size = vAttribs[3];
        #endif
                float offset = 0.5 * (1.0 - size * (1.0 + 2.0 * vAttribs[1]));
                mediump float ink = clamp((color.r - 0.5 * (1.0 - size)) / size, 0.0, 1.0);
                // The halo quad paints the ring only (under translucent ink a solid halo shows through),
                // ending half a pixel inside the ink's edge.
                mediump float alpha = vAttribs[1] > 0.0
                    ? clamp((color.r - offset) / size, 0.0, 1.0) - clamp((color.r - 0.5) / size, 0.0, 1.0)
                    : ink;
                color = max(alpha, 0.0) * vColor;
            }
        #ifdef LIGHTING_FSH
            color = applyLighting(color, normalize(vNormal));
        #endif
            // Fade out once the haze is solid, or it reads as floating text (mapbox's 0.9).
            glFragColor = applyFog(color) * fogLabelFade();
        }
    )GLSL";

    static const std::string pointVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        #if defined(LIGHTING_FSH) || defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
        attribute vec3 aVertexBinormal;
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        #endif
        attribute vec4 aVertexAttribs;
        uniform float uBinormalScale;
        uniform float uSDFScale;
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform vec4 uColorTable[16];
        uniform float uWidthTable[16];
        #ifdef OFFSET
        uniform float uStrokeWidthTable[16];
        #endif
        #ifdef PATTERN
        uniform vec2 uUVScale;
        varying highp_opt vec2 vUV;
        #endif
        varying lowp vec4 vColor;
        varying mediump vec4 vAttribs;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float size = uWidthTable[styleIndex];
            vec3 pos = aVertexPosition;
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            vec3 delta = aVertexBinormal * (uBinormalScale * size);
            vec4 color = aVertexAttribs[1] > 1.0 ? vec4(1.0, 1.0, 1.0, 1.0) : uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = uUVScale * aVertexUV;
        #endif
        #ifdef OFFSET
            float halo = uStrokeWidthTable[styleIndex];
            float offset = 0.5 - 0.5 * uSDFScale / size * (1.0 + halo);
        #else
            float halo = 0.0;
            float offset = 0.5 - 0.5 * uSDFScale / size;
        #endif
            // [1] = halo width (0 for the glyph): the halo is its own pass (TileLayerBuilder), as in labelFsh.
            vAttribs = vec4(aVertexAttribs[1], halo, offset, size / uSDFScale);
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, lightingNormal(aVertexNormal));
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = lightingNormal(aVertexNormal);
        #endif
            // Terrain at the extruded corner: a flat plate at the anchor height sinks uphill.
            setTerrainSlopeVaryings(pos + delta);
            highp vec3 terrainPos = applyTerrain(pos + delta);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        }
    )GLSL";

    static const std::string pointFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        varying highp_opt vec2 vUV;
        #endif
        varying lowp vec4 vColor;
        varying mediump vec4 vAttribs;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
        #ifdef PATTERN
            lowp vec4 color = texture2D(uPattern, vUV);
            if (vAttribs[0] > 0.0) {
                color = color * vColor;
            } else {
                mediump float alpha = clamp((color.r - vAttribs[2]) * vAttribs[3], 0.0, 1.0);
                if (vAttribs[1] > 0.0) {
                    // Ring only, as labelFsh.
                    alpha -= clamp((color.r - 0.5) * vAttribs[3], 0.0, 1.0);
                }
                color = max(alpha, 0.0) * vColor;
            }
        #else
            lowp vec4 color = vColor;
        #endif
        #ifdef TERRAIN
            // transparent fragments must not write depth or they block later layers
            if (color.a < 0.004) discard;
        #endif
            color = applyTerrainShading(color);
        #ifdef LIGHTING_FSH
            glFragColor = applyFog(applyLighting(color, normalize(vNormal)));
        #else
            glFragColor = applyFog(color);
        #endif
        }
    )GLSL";

    static const std::string lineVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        #if defined(TERRAIN) && defined(SPAN)
        // CPU-resolved deck chord height; the sentinel (< -1e29) means unresolved, stay on the terrain.
        attribute float aVertexBase;
        uniform float uBaseScale;
        #endif
        #if defined(LIGHTING_FSH) || defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
        attribute vec3 aVertexBinormal;
        attribute vec4 aVertexAttribs;
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        uniform vec2 uUVScale;
        uniform float uStrokeScaleTable[16];
        #endif
        uniform float uBinormalScale;
        #ifdef TERRAIN
        uniform highp vec2 uScreenScale; // x = viewport aspect (w/h), y = NDC height of one line-width unit
        uniform highp float uBinormalUnitScale; // packed binormal -> its length in line widths
        #endif
        // Position in the target tile, for lineFsh's clip.
        varying mediump vec2 vTileUnit;
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform vec4 uColorTable[16];
        uniform float uWidthTable[16];
        #ifdef OFFSET
        uniform float uOffsetTable[16];
        #endif
        #ifdef GAPWIDTH
        uniform float uGapWidthTable[16];
        #endif
        #ifdef BLUR
        uniform float uBlurTable[16];
        #endif
        #ifdef PATTERN
        varying highp_opt vec2 vUV;
        #endif
        varying lowp vec4 vColor;
        varying highp_opt vec2 vDist;
        varying highp_opt float vWidth;
        varying highp_opt float vInnerWidth;
        varying highp_opt float vBlur;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float width = uWidthTable[styleIndex];
        #ifdef GAPWIDTH
            // The gap is cut in the fragment shader: no extra geometry, the line's own joins and caps.
            float innerWidth = uGapWidthTable[styleIndex];
        #else
            float innerWidth = 0.0;
        #endif
            float roundedWidth = width > 0.0 ? width + 1.0 : 0.0;
            float gamma = 0.5;
            vec3 pos = aVertexPosition;
            vec3 delta = aVertexBinormal * (uBinormalScale * roundedWidth);
        #ifdef OFFSET
            float offset = uOffsetTable[styleIndex];
            delta = delta - aVertexBinormal * (uBinormalScale * offset * aVertexAttribs[2]);
        #endif
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            vec4 color = uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = uUVScale * aVertexUV + vec2(aVertexAttribs[3] * roundedWidth * uStrokeScaleTable[styleIndex], 0.0);
        #endif
            vDist = vec2(aVertexAttribs[1], aVertexAttribs[2]) * (roundedWidth * gamma); // will be 0,0 for polygons
            vWidth = width > 0.0 ? (width - 1.0) * gamma + 1.0 : 1.0; // will be 1 for polygons
            // A fraction of the outer edge: vDist is scaled by roundedWidth, not width.
            vInnerWidth = width > 0.0 ? vWidth * (innerWidth / width) : 0.0;
        #ifdef BLUR
            vBlur = uBlurTable[styleIndex] * gamma;
        #else
            vBlur = 0.0;
        #endif
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, lightingNormal(aVertexNormal));
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = lightingNormal(aVertexNormal);
        #endif
        #ifdef TERRAIN
            // tangram's line model plus a ceiling: theirs grows without bound towards the camera.
            setTerrainSlopeVaryings(pos);
        #ifdef TERRAIN_SPHERICAL
            vTileUnit = terrainSphereTileUnit(pos);
        #else
            vTileUnit = pos.xy * uTileUnitScale + uTileUnitOffset;
        #endif
            highp vec3 centerPos = applyTerrain(pos);
        #ifdef SPAN
            highp float spanZ = aVertexBase * uBaseScale + uElevationScale.w;
            bool spanResolved = aVertexBase > -1.0e29;
            if (spanResolved) {
                centerPos.z = spanZ;
            }
        #endif
            applyShadowPos(centerPos);
            highp vec4 centerClip = uMVPMatrix * vec4(centerPos, 1.0);
            highp vec3 edgePos = applyTerrain(pos + delta);
        #ifdef SPAN
            // The chord varies along the line, not across it: the edge takes the centre's height.
            if (spanResolved) {
                edgePos.z = spanZ;
                setSpanFlatShading();
            }
        #endif
            highp vec4 edgeClip = uMVPMatrix * vec4(edgePos, 1.0);
            highp vec2 edgeDir = edgeClip.xy / edgeClip.w - centerClip.xy / centerClip.w;
            edgeDir = vec2(edgeDir.x * uScreenScale.x, edgeDir.y); // NDC is anisotropic
            highp float edgeLen = length(edgeDir);
            // Ceiling = this vertex's own extrusion (cap corners, barbs); the binormal is packed, so scale it.
            highp float nominalLen = roundedWidth * length(aVertexBinormal * uBinormalUnitScale) * uScreenScale.y;
            if (edgeLen > nominalLen && nominalLen > 0.0) {
                highp float shrink = nominalLen / edgeLen;
                edgeDir = edgeDir * shrink;
                highp vec2 offset = vec2(edgeDir.x / uScreenScale.x, edgeDir.y);
                // Depth from the shrunk terrain position: centerClip.z sinks the edge on a cross-slope.
                highp vec4 depthClip = uMVPMatrix * vec4(mix(centerPos, edgePos, shrink), 1.0);
                gl_Position = applyDepthBias(vec4((centerClip.xy / centerClip.w + offset) * depthClip.w, depthClip.z, depthClip.w));
            } else {
                gl_Position = applyDepthBias(uMVPMatrix * vec4(edgePos, 1.0));
            }
        #ifdef TERRAIN_SPHERICAL
            if (uDrapeBake != 0.0) {
                gl_Position = drapeBakeClip(uMVPMatrix, pos + delta);
            }
        #endif
        #else
            vTileUnit = pos.xy * uTileUnitScale + uTileUnitOffset;
            setTerrainSlopeVaryings(pos + delta);
            highp vec3 flatTerrainPos = applyTerrain(pos + delta);
            applyShadowPos(flatTerrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(flatTerrainPos, 1.0));
        #endif
        }
    )GLSL";

    static const std::string lineFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        varying highp_opt vec2 vUV;
        #endif
        uniform mediump float uAntialiasScale;
        uniform highp vec2 uTileUnitScale;
        varying mediump vec2 vTileUnit;
        varying lowp vec4 vColor;
        varying highp_opt vec2 vDist;
        varying highp_opt float vWidth;
        varying highp_opt float vInnerWidth;
        varying highp_opt float vBlur;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            // Clip to the tile, or a neighbour's road draws twice at two heights; 0 = no elevation, off.
            if (uTileUnitScale != vec2(0.0)) {
                if (vTileUnit.x < -0.0005 || vTileUnit.x > 1.0005 || vTileUnit.y < -0.0005 || vTileUnit.y > 1.0005) {
                    discard;
                }
            }
            // Antialias ramp in device pixels (widths are unscaled-DPI); `line-blur` widens it.
            lowp float ramp = 1.0 + vBlur * uAntialiasScale;
            // The inner ramp fades into the gap, as mapbox's, so it does not eat into the strip.
            float d = length(vDist);
            float dist = (vWidth - d) * uAntialiasScale;
            if (vInnerWidth > 0.0) {
                dist = min(dist, (d - vInnerWidth) * uAntialiasScale + ramp);
            }
            lowp float a = clamp(dist / ramp, 0.0, 1.0);
        #ifdef PATTERN
            lowp vec4 color = texture2D(uPattern, vUV) * vColor * a;
        #else
            lowp vec4 color = vColor * a;
        #endif
            color = applyDrapeMask(color, vTileUnit);
        #ifdef TERRAIN
            // transparent fragments must not write depth or they block later layers
            if (color.a < 0.004) discard;
        #endif
        #ifdef COVERAGE
            glFragColor = vec4(color.a);
        #else
            color = applyTerrainShading(color);
        #ifdef LIGHTING_FSH
            glFragColor = applyFog(applyLighting(color, normalize(vNormal)));
        #else
            glFragColor = applyFog(color);
        #endif
        #endif
        }
    )GLSL";

    static const std::string polygonVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        #if defined(LIGHTING_FSH) || defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
        attribute vec4 aVertexAttribs;
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        uniform vec2 uUVScale;
        // 1 = patterned fill, 0 = plain, so both share one draw.
        uniform float uPatternTable[16];
        #endif
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform vec4 uColorTable[16];
        #ifdef PATTERN
        varying highp_opt vec3 vUV; // .xy uv, .z pattern flag
        #endif
        varying lowp vec4 vColor;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif
        #ifdef DRAPE_MASK
        // Position in the target tile, for the drape mask lookup.
        varying mediump vec2 vTileUnit;
        #endif
        #if defined(SPAN) && defined(TERRAIN)
        // Bridge bed chord height (CPU, internal z units); the sentinel means stay on the terrain.
        attribute float aVertexBase;
        uniform float uBaseScale;
        // Position along the chord, unclamped: cut past the portals.
        attribute float aVertexChord;
        varying highp_opt float vSpanChord;
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            vec3 pos = aVertexPosition;
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
        #ifdef DRAPE_MASK
            vTileUnit = pos.xy * uTileUnitScale + uTileUnitOffset;
        #endif
        #if defined(SPAN) && defined(TERRAIN)
            vSpanChord = aVertexChord;
        #endif
            vec4 color = uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = vec3(uUVScale * aVertexUV, uPatternTable[styleIndex]);
        #endif
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, lightingNormal(aVertexNormal));
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = lightingNormal(aVertexNormal);
        #endif
            setTerrainSlopeVaryings(pos);
            highp vec3 terrainPos = applyTerrain(pos);
        #if defined(SPAN) && defined(TERRAIN)
            if (aVertexBase > -1.0e29) {
                terrainPos.z = aVertexBase * uBaseScale + uElevationScale.w;
                setSpanFlatShading();
            }
        #endif
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        #ifdef TERRAIN_SPHERICAL
            if (uDrapeBake != 0.0) {
                gl_Position = drapeBakeClip(uMVPMatrix, pos);
            }
        #endif
        }
    )GLSL";

    static const std::string polygonFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        // .xy uv, .z pattern flag
        varying highp_opt vec3 vUV;
        #endif
        varying lowp vec4 vColor;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif
        #ifdef DRAPE_MASK
        varying mediump vec2 vTileUnit;
        #endif
        #if defined(SPAN) && defined(TERRAIN)
        varying highp_opt float vSpanChord;
        #endif

        void main(void) {
        #if defined(SPAN) && defined(TERRAIN)
            // Past a portal the bed would hide the quay.
            if (vSpanChord < 0.0 || vSpanChord > 1.0) {
                discard;
            }
        #endif
        #ifdef PATTERN
            lowp vec4 color = mix(vColor, texture2D(uPattern, vUV.xy) * vColor, vUV.z);
        #else
            lowp vec4 color = vColor;
        #endif
        #ifdef DRAPE_MASK
            color = applyDrapeMask(color, vTileUnit);
        #endif
        #ifdef TERRAIN
            // transparent fragments must not write depth or they block later layers
            if (color.a < 0.004) discard;
        #endif
        #ifdef COVERAGE
            glFragColor = vec4(color.a);
        #else
            color = applyTerrainShading(color);
        #ifdef LIGHTING_FSH
            glFragColor = applyFog(applyLighting(color, normalize(vNormal)));
        #else
            glFragColor = applyFog(color);
        #endif
        #endif
        }
    )GLSL";

    // Extrusion contact shadow: one capsule quad per footprint edge, drawn into an offscreen mask
    // under MIN blending and multiplied into the frame once.
    static const std::string polygon3DGroundVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        attribute vec3 aVertexNormal;
        attribute vec3 aVertexBinormal;
        attribute vec2 aVertexUV;
        attribute float aVertexHeight;
        uniform mat4 uMVPMatrix;
        uniform mat3 uTileMatrix;
        uniform float uUVScale;
        uniform float uHeightScale;
        uniform float uBinormalScale;
        uniform vec4 uColorTable[16];
        varying highp_opt vec2 vTilePos;
        varying mediump vec3 vSegment;
        varying lowp float vGroundBlend;

        void main(void) {
            vec3 pos = applyTerrain(aVertexPosition) + aVertexNormal * (aVertexHeight * uHeightScale);
            vTilePos = (uTileMatrix * vec3(aVertexUV * uUVScale, 1.0)).xy;
            // (along, across, length) in shadow radii; affine, so interpolation is exact.
            vSegment = aVertexBinormal * uBinormalScale;
            // Tile fade via the colour's alpha: the extrusion fades in by growing, the shadow must too.
            vGroundBlend = uColorTable[0].a;
            gl_Position = applyDepthBias(uMVPMatrix * vec4(pos, 1.0));
        }
    )GLSL";

    static const std::string polygon3DGroundFsh = R"GLSL(
        uniform mediump vec2 uGroundAOParams; // x = intensity, y = attenuation
        varying highp_opt vec2 vTilePos;
        varying mediump vec3 vSegment;
        varying lowp float vGroundBlend;

        void main(void) {
            // Half-open tile clip: under overzoom a neighbour would cast the shadow again at its height.
            if (vTilePos.x < 0.0 || vTilePos.x >= 1.0 || vTilePos.y < 0.0 || vTilePos.y >= 1.0) {
                discard;
            }
            // Per-fragment segment distance: an interpolated one facets the rounded caps.
            mediump float t = clamp(vSegment.x, 0.0, vSegment.z);
            // Fully occluded under the building, alongside the edge only; the next edge covers the rest.
            mediump float across = (vSegment.y > 0.0 && vSegment.x == t) ? 0.0 : vSegment.y;
            mediump float dist = min(1.0, length(vec2(vSegment.x - t, across)));
            // (1 - d)^k, k = ground-attenuation: k > 1 reaches zero with zero slope, no crease.
            mediump float occlusion = pow(1.0 - dist, uGroundAOParams.y);
            mediump float f = 1.0 - uGroundAOParams.x * vGroundBlend * occlusion;
            glFragColor = vec4(f, f, f, 1.0);
        }
    )GLSL";

    static const std::string polygon3DVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        attribute vec3 aVertexNormal;
        attribute vec3 aVertexBinormal;
        attribute vec2 aVertexUV;
        attribute float aVertexHeight;
        // CPU-resolved base (internal z units), identical for every vertex of a building across tiles.
        attribute float aVertexBase;
        attribute vec4 aVertexAttribs;
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform mat3 uTileMatrix;
        uniform float uUVScale;
        uniform float uHeightScale;
        #ifdef TERRAIN
        uniform float uBaseScale;   // internal z units -> this vertex frame (1 / frameScaleZ)
        // 1 = every vertex takes the base (a bridge deck); else the base ring stays on the ground.
        uniform float uFloatingBase;
        #endif
        // Height the shadow map was baked at: the caster ignores building-height-view-scale.
        uniform float uShadowHeightScale;
        uniform vec4 uColorTable[16];
        varying highp_opt vec2 vTilePos;
        varying lowp vec4 vColor;
        #ifdef LIGHTING_FSH
        varying lowp float vWallT;
        varying lowp float vSideVertex;
        varying mediump vec3 vNormal;
        #endif
        #ifdef TERRAIN_SHADOW
        // For the shadow's N.L; vNormal exists only with per-fragment lighting.
        varying mediump vec3 vShadowNormal;
        #endif
        #ifdef SPAN_DRAPE
        // 1 on the roof, 0 on walls: only the roof wears the road.
        varying lowp float vSpanRoof;
        #endif
        #if defined(SPAN) && defined(TERRAIN)
        // Position along the chord, unclamped: past the portals the deck is ground.
        attribute float aVertexChord;
        varying highp_opt float vSpanChord;
        // Height above the ground (internal z units): the overhang wears the ground only where on it.
        varying mediump float vSpanAbove;
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            // 0 roof, 1 wall, between on the one-quad bevel band (TileLayerBuilder edge radius).
            float sideVertex = aVertexAttribs[1] * (1.0 / 127.0);
            // Facade gradient (packGradientT): 0 at the foot, 1 past its reach.
            float wallT = aVertexAttribs[3] * (1.0 / 127.0);
            vec3 pos = aVertexPosition;
            // Level roof from one base elevation, ground ring on the terrain (mapbox's base-alignment
            // terrain + height-alignment flat).
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            vec3 groundPos = applyTerrain(pos);
            vec3 basePos = groundPos;
            // Unresolved base falls back to the ground: a sheared roof beats a lost or buried building.
        #ifdef TERRAIN_SPHERICAL
            // The base is an offset along the sphere normal (18-globe.md).
            highp vec3 baseUp = normalize(terrainSpherePoint(pos));
            // Before extrusion: on a sphere vertex xy is not its tile position.
            highp vec2 sphereTileUnit = terrainSphereTileUnit(pos);
        #ifdef TERRAIN
            if ((aVertexHeight > 0.0 || uFloatingBase > 0.5) && aVertexBase > -1.0e29) {
                basePos = pos + baseUp * (aVertexBase * uBaseScale + uElevationScale.w);
            }
        #endif
        #else
            float groundZ = groundPos.z;
            float baseZ = groundZ;
        #ifdef TERRAIN
            if ((aVertexHeight > 0.0 || uFloatingBase > 0.5) && aVertexBase > -1.0e29) {
                baseZ = aVertexBase * uBaseScale + uElevationScale.w;
            }
        #endif
            basePos = vec3(pos.xy, baseZ);
        #endif
            pos = basePos + aVertexNormal * (aVertexHeight * uHeightScale);
            vec3 normal = normalize(mix(aVertexNormal, aVertexBinormal, sideVertex));
            applyShadowPos(basePos + aVertexNormal * (aVertexHeight * uShadowHeightScale), normal);
            // The shadow offset is world-space; lighting from here is in view east/north/up.
            normal = lightingNormal(normal);
        #ifdef TERRAIN_SHADOW
            vShadowNormal = normal;
        #endif
            vec4 color = uColorTable[styleIndex];
            // Per-vertex overzoom clip (a centroid test drops buildings spanning tiles); y flipped to tile space.
        #ifdef TERRAIN_SPHERICAL
            vTilePos = sphereTileUnit;
        #else
            vec2 vertexTile = aVertexPosition.xy * uUVScale;
            vTilePos = (uTileMatrix * vec3(vertexTile.x, 1.0 - vertexTile.y, 1.0)).xy;
        #endif
        #ifdef LIGHTING_VSH
            // The shadow is multiplied in per fragment (ambient included).
            vColor = applyLighting3D(color, normal, wallT, sideVertex, 1.0, 1.0);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = normal;
            vWallT = wallT;
            vSideVertex = sideVertex;
        #endif
        #ifdef SPAN_DRAPE
            vSpanRoof = 1.0 - sideVertex;
        #endif
        #if defined(SPAN) && defined(TERRAIN)
            vSpanChord = aVertexChord;
        #ifdef TERRAIN_SPHERICAL
            vSpanAbove = dot(pos - groundPos, baseUp) / max(uBaseScale, 1.0e-6);
        #else
            vSpanAbove = (pos.z - groundZ) / max(uBaseScale, 1.0e-6);
        #endif
        #endif
            gl_Position = applyDepthBias(uMVPMatrix * vec4(pos, 1.0));
        }
    )GLSL";

    static const std::string polygon3DFsh = R"GLSL(
        varying highp_opt vec2 vTilePos;
        varying lowp vec4 vColor;
        #ifdef SPAN_DRAPE
        // uSpanDrapeTransform maps tile position into the drape tile's uv.
        uniform sampler2D uSpanDrapeTexture;
        uniform mediump vec4 uSpanDrapeTransform;
        // Ground light for a flat up-facing surface, constant so CPU-resolved; 1 for `colors-prelit`.
        uniform mediump vec3 uSpanDrapeLight;
        varying lowp float vSpanRoof;
        // Ground drape worn by the roof past the portals; uGroundDrape = 0 when the tile has none.
        uniform sampler2D uGroundDrapeTexture;
        uniform mediump vec4 uGroundDrapeTransform; // target tile's share of the owner's drape
        uniform mediump float uGroundDrape;
        uniform mediump float uSpanGroundTolerance;
        #endif
        #if defined(SPAN) && defined(TERRAIN)
        varying highp_opt float vSpanChord;
        varying mediump float vSpanAbove;
        #endif
        #ifdef TERRAIN_SHADOW
        varying mediump vec3 vShadowNormal;
        #endif
        #ifdef LIGHTING_FSH
        varying lowp float vWallT;
        varying lowp float vSideVertex;
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            // Half-open [0,1) so overzoomed copies partition; an overlap z-fights on the roofline.
            if (vTilePos.x < 0.0 || vTilePos.x >= 1.0 || vTilePos.y < 0.0 || vTilePos.y >= 1.0) {
                discard;
            }
            // Own-normal N.L: a roof is its own caster and speckles at the minimum bias.
            mediump float shadow = 1.0;
            // Only map occlusion dims the sky, or a shadowed wall barely darkens at mapbox's 0.8 ambient.
            mediump float skyShadow = 1.0;
        #ifdef TERRAIN_SHADOW
            shadow = shadowFactorSlopeParts(max(0.0, dot(normalize(vShadowNormal), uSunDir)), skyShadow);
        #endif
            lowp vec4 surfaceColor = vColor;
        #if defined(SPAN) || defined(SPAN_DRAPE)
            // The span drape was baked at vTilePos, unflipped.
            highp_opt vec2 spanUV = vec2(vTilePos.x, 1.0 - vTilePos.y);
            // A roof triangle is emitted whole into every tile it touches: cut at the tile, as mapbox.
            if (spanUV.x < 0.0 || spanUV.x > 1.0 || spanUV.y < 0.0 || spanUV.y > 1.0) {
                discard;
            }
        #endif
        #ifdef SPAN_DRAPE
            highp_opt vec2 drapeUV = spanUV * uSpanDrapeTransform.zw + uSpanDrapeTransform.xy;
            lowp vec4 draped = texture2D(uSpanDrapeTexture, drapeUV);
            // No road past the baked bounds, not the clamped edge texel.
            draped *= step(0.0, drapeUV.x) * step(drapeUV.x, 1.0) * step(0.0, drapeUV.y) * step(drapeUV.y, 1.0);
        #if defined(SPAN) && defined(TERRAIN)
            // Past the portals: ground where the roof lies on it, else nothing.
            if (vSpanChord < 0.0 || vSpanChord > 1.0) {
                draped = (uGroundDrape > 0.5 && vSpanAbove < uSpanGroundTolerance)
                    ? texture2D(uGroundDrapeTexture, spanUV * uGroundDrapeTransform.zw + uGroundDrapeTransform.xy)
                    : vec4(0.0);
            }
        #endif
        #endif
        #ifdef LIGHTING_FSH
            // Shadow dims the sun inside the lighting; multiplied after, back-facing walls go black.
            surfaceColor = applyLighting3D(surfaceColor, normalize(vNormal), vWallT, vSideVertex, shadow, skyShadow);
        #endif
        #ifdef SPAN_DRAPE
            // A draped pixel is finished ground: ground light once, not the facade model.
            lowp vec3 drapeColor = draped.rgb * uSpanDrapeLight;
        #ifdef LIGHTING_FSH
            // As backgroundFsh; the per-vertex path multiplies the whole fragment below.
            drapeColor *= shadow;
        #endif
            // The bake is premultiplied.
            surfaceColor = vec4(surfaceColor.rgb * (1.0 - draped.a * vSpanRoof) + drapeColor * vSpanRoof, surfaceColor.a);
        #endif
            glFragColor = applyFog(surfaceColor);
        #ifndef LIGHTING_FSH
            glFragColor.rgb *= shadow;
        #endif
        }
    )GLSL";
}

#endif
