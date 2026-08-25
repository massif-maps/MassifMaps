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
        A_VERTEXHEIGHT,
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
        U_COLORTABLE,
        U_WIDTHTABLE,
        U_OFFSETTABLE,
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
        U_SHADOWMATRIX,
        U_SHADOWTEXTURE,
        U_SHADOWPARAMS,
        U_SHADOWBIAS,
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
        U_LABELOCCLUSIONPARAMS
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
        // How many cascades the shadow lookup is compiled for (none of these = one). The count is
        // a compile-time constant because it decides how many matrices the vertex stage applies and
        // how many highp varyings it interpolates, and that - not the PCF taps - is what the
        // shadowed surface costs (docs/rendering/08-lighting-sky-fog.md).
        SHADOW_CASCADES2_FLAG = 8192,
        SHADOW_CASCADES3_FLAG = 16384,
        SHADOW_CASCADES4_FLAG = 32768,
        // The shadow of the terrain SURFACE, computed once per screen pixel into a half-resolution
        // mask (OUT) and then sampled by every surface that covers that pixel (IN). The lookup is
        // the most expensive thing a shadowed fragment does and the ground is drawn over the whole
        // screen, sometimes twice - once as the drape and once as the paint over it.
        SHADOW_MASK_OUT_FLAG = 65536,
        SHADOW_MASK_IN_FLAG = 131072,
        // One tap instead of the kernel. For 3D extrusion fragments: a wall is shadowed or lit over
        // almost all of its area and its own silhouette is what the eye reads, so the kernel buys
        // far less there than on terrain, where it is what makes a finite map look like a shadow.
        SHADOW_SINGLE_TAP_FLAG = 262144,
        // The shadow map IS the depth buffer, sampled directly, instead of a packed-RGB copy of it
        // in a colour texture. Everything ES3-class; the packed path stays for the rest.
        SHADOW_DEPTH_TEXTURE_FLAG = 524288,
        // The program is compiled as GLSL ES 3.00 rather than 1.00. Only what needs it asks for it;
        // the keyword differences are handled by a prelude in createShaderProgram.
        ESSL3_FLAG = 1048576,
        // Hardware depth comparison (sampler2DShadow). Implies ESSL3 - the type does not exist in
        // GLSL ES 1.00, and GL_EXT_shadow_samplers is not advertised on an ES3 context because the
        // feature is core there.
        SHADOW_HW_FLAG = 2097152,
        // Terrain sun on UNDRAPED 2D geometry. A separate flag from TERRAIN_LIGHT because that one
        // marks the surface shaders, which declare the same uniforms themselves - one name twice in
        // a stage is a link error.
        GEOMETRY_LIGHT_FLAG = 4194304,
        // A label asks a screen depth texture of the 3D occluders whether its ANCHOR is behind
        // one, and fades the whole label out if it is (mapbox's model - see labelVsh).
        LABEL_OCCLUSION_FLAG = 8388608
    };

    static const std::map<std::string, int> attribMap = {
        { "aVertexPosition", A_VERTEXPOSITION },
        { "aVertexUV",       A_VERTEXUV },
        { "aVertexNormal",   A_VERTEXNORMAL },
        { "aVertexBinormal", A_VERTEXBINORMAL },
        { "aVertexHeight",   A_VERTEXHEIGHT },
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
        { "uColorTable",       U_COLORTABLE },
        { "uWidthTable",       U_WIDTHTABLE },
        { "uOffsetTable",      U_OFFSETTABLE },
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
        { "uShadowMatrix",      U_SHADOWMATRIX },
        { "uShadowTexture",     U_SHADOWTEXTURE },
        { "uShadowParams",      U_SHADOWPARAMS },
        { "uShadowBias",        U_SHADOWBIAS },
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
        { "uLabelOcclusionParams", U_LABELOCCLUSIONPARAMS }
    };

    static const std::map<unsigned int, std::string> flagDefineMap = {
        { TRANSFORM_FLAG,   "TRANSFORM" },
        { OFFSET_FLAG,      "OFFSET" },
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
        { GEOMETRY_LIGHT_FLAG, "GEOMETRY_LIGHT" }
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
        #ifdef TERRAIN_DEPTH_BIAS
        uniform float uDepthBias;     // NDC-constant component (scaled by w)
        uniform float uDepthBiasClip; // clip-constant component: in eye units the slack
                                      // grows with distance, tracking the growth of the
                                      // piecewise-linear interpolation error between the
                                      // surface and geometry meshes (tangram depth_shift)
        uniform float uLayerDepthOffset; // painter-order model: (proxy - layer). 0 in slack mode
        uniform float uDepthShift;       // painter-order near-camera separation boost
        uniform float uDepthClearance;   // METRE-constant clearance: proj[2][3] * metres (see below)
        // Three depth terms, selected by which uniforms are non-zero:
        //  - slack (occluder) model: (uDepthBias*w + uDepthBiasClip) pulls the draw towards the
        //    viewer so draped content clears the surface pre-pass;
        //  - painter-order (tangram): uLayerDepthOffset*(DELTA*w + uDepthShift), DELTA = 2^-19 -
        //    a fixed per-layer delta, no occluder and no distance-growing slack;
        //  - uDepthClearance: worth the SAME METRES at every range. ndc = -proj[2][2] + proj[2][3]/d,
        //    so moving a vertex by c gives a clip term proj[2][3]*c/w - the 1/w below. Constant-NDC
        //    is worth distance^2/near and constant-CLIP distance/near; only this one is what a
        //    draped LINE needs, since its chord over relief is a fixed number of metres.
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
        // Tile-local -> light clip space, one matrix per cascade. The matrices are built per tile
        // so their input stays in [0,1] and float precision is never asked to hold a world
        // coordinate. EVERY cascade is computed here: which one a fragment ends up using is
        // decided in the fragment stage from the result, and a varying cannot be written
        // conditionally on something only the fragment stage knows. Compiled for the cascade count
        // in use, because each one is a matrix per vertex and a highp varying per fragment.
        uniform highp mat4 uShadowMatrix[SHADOW_CASCADES];
        // NORMAL OFFSET, mapbox's model (3d-style/shaders/_prelude_shadow.vertex.glsl): the point
        // looked up in the shadow map is pushed OUT ALONG ITS OWN NORMAL, per cascade, in tile-local
        // units. Acne then goes away by moving the sample sideways instead of by lifting its depth,
        // so the depth bias can stay small and the shadow stays ATTACHED to the wall that casts it -
        // which is the whole difference on a building. A dedicated sun uniform: uSunDir belongs to
        // the fragment stage here, and one name declared in two blocks of the same stage is a link
        // error.
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
            // Scaled by how far the surface faces AWAY from the sun: a face lit head-on needs
            // almost no offset, a grazing one needs the full texel. mapbox's curve verbatim.
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
        // No normal to hand: the terrain surface and draped content take their N.L from the DEM in
        // the fragment stage, where a vertex offset cannot reach. A zero normal is a zero offset.
        void applyShadowPos(highp vec3 pos) {
            applyShadowPos(pos, vec3(0.0));
        }
        #else
        void applyShadowPos(highp vec3 pos, mediump vec3 normal) {
        }
        void applyShadowPos(highp vec3 pos) {
        }
        #endif
        // Vertex frame units -> tile units (1 for the surface, whose vertices ARE the unit square).
        // Declared in every mode: the flat line path carries it to the fragment stage too, and a
        // uniform referenced without being declared fails the compile.
        uniform highp vec2 uTileUnitScale;
        uniform highp vec2 uTileUnitOffset;
        #ifdef TERRAIN
        uniform highp sampler2D uElevationTexture;
        uniform highp vec4 uElevationUV;     // elevation texture uv = uv.xy + pos.xy * uv.zw
        uniform vec4 uElevationDecode;       // meters = dot(texture sample, decode) + offset
        uniform float uElevationOffset;      // the constant term: a 2-channel texture has no free channel to carry it
        uniform highp vec4 uElevationScale;  // x: meters to vertex z units (equator), y/z: mercator y = y + pos.y * z, w: vertex frame z offset
        uniform highp vec4 uElevationTexelSize; // xy: texture size in texels, zw: 1 / size
        uniform highp vec2 uElevationLatticeCell; // regular-grid surface cell size in elevation-uv units (0 = off = sample the full DEM detail)
        uniform highp vec4 uTerrainEdgeCoarsening; // lattice cell scale (2^k, 1 = off) on the west/east/south/north tile edge

        // GPU draping: the vertex z is REPLACED with the height sampled from the elevation
        // texture, the same texture for every layer, so all of them agree exactly.
        // The bilinear filter is MANUAL - 4 samples at exact texel centres plus mix - because
        // several mobile GPUs filter VERTEX-stage fetches as NEAREST whatever is requested, and
        // that deviates from the depth-writing surface by up to a full texel step (tens of metres
        // on a cliff). At texel centres both filters return the same texel, so this is identical
        // everywhere, and it matches ElevationTileGrid::sampleHeight on the CPU side.
        float sampleElevation(highp vec2 uv) {
            // The elevation texture is LUMINANCE_ALPHA: the height's high byte arrives in .rgb and
            // its low byte in .a, so the decode is linear in both and the constant term needs a
            // uniform of its own (RGBA had a spare channel pinned to 1 to carry it).
            return dot(texture2D(uElevationTexture, uv), uElevationDecode) + uElevationOffset;
        }
        // Full DEM detail: manual bilinear of the elevation texture at uv (4 texel-center taps).
        // DEM_HW_FILTER collapses it to ONE hardware-filtered fetch, which is what tangram's
        // terrain vertex does. It exists because some mobile GPUs ignore LINEAR for vertex texture
        // fetch and return NEAREST, which shows as terraced geometry - so it is a measurement
        // switch, not a default, until a device says the filtering is honoured.
        #ifdef DEM_HW_FILTER
        float demMeters(highp vec2 uv) {
            return sampleElevation(uv);
        }
        #else
        float demMeters(highp vec2 uv) {
            highp vec2 texelPos = uv * uElevationTexelSize.xy - 0.5;
            highp vec2 texelBase = floor(texelPos);
            highp vec2 f = texelPos - texelBase;
            highp vec2 uv00 = (texelBase + 0.5) * uElevationTexelSize.zw;
            float h00 = sampleElevation(uv00);
            float h10 = sampleElevation(uv00 + vec2(uElevationTexelSize.z, 0.0));
            float h01 = sampleElevation(uv00 + vec2(0.0, uElevationTexelSize.w));
            float h11 = sampleElevation(uv00 + uElevationTexelSize.zw);
            return mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
        }
        #endif
        vec3 applyTerrain(vec3 pos) {
            highp vec2 uv = uElevationUV.xy + pos.xy * uElevationUV.zw;
            float meters;
            if (uElevationLatticeCell.x != 0.0) {
                // LATTICE CLAMP: take the 4 surrounding grid-corner heights (each a full DEM
                // bilinear) and interpolate them with the SAME two-triangle split the surface mesh
                // uses, so draped geometry follows the surface everywhere, not only at the nodes.
                // A bilinear blend instead leaves an in-cell twist that exceeds the (near zero)
                // painter-order slack at large cells and cracks draped lines.
                // buildRegularGridSurface emits (a,b,c),(a,c,d) with a=(i,j)..d=(i,j+1), and vertex
                // y is 1-v, so in elevation-uv fg-space d=(0,0)=H00, c=(1,0)=H10, a=(0,1)=H01,
                // b=(1,1)=H11 and the shared edge is the ANTI-diagonal fg.x+fg.y=1. Match it.
                // CROSS-LOD STITCHING: a coarser neighbour's lattice is a strict subset of this
                // one's, so scaling the cell along a shared edge reproduces its chords exactly
                // (factors are 1 by default; a corner sits on every lattice, so double scaling is
                // harmless). The edge test is in TILE units - draped CONTENT arrives in its own
                // frame and must be converted, or the ground is stitched and the road on it is not.
                highp vec2 unitPos = pos.xy * uTileUnitScale;
                highp vec2 cell = uElevationLatticeCell;
                if (unitPos.x < 0.00001) cell.y *= uTerrainEdgeCoarsening.x;       // west edge
                else if (unitPos.x > 0.99999) cell.y *= uTerrainEdgeCoarsening.y;  // east edge
                if (unitPos.y < 0.00001) cell.x *= uTerrainEdgeCoarsening.z;       // south edge
                else if (unitPos.y > 0.99999) cell.x *= uTerrainEdgeCoarsening.w;  // north edge
                highp vec2 rel = (uv - uElevationUV.xy) / cell;
                highp vec2 gi = floor(rel);
                highp vec2 fg = rel - gi;
                highp vec2 uv00 = uElevationUV.xy + gi * cell;
                float H00 = demMeters(uv00);
                float H10 = demMeters(uv00 + vec2(cell.x, 0.0));
                float H01 = demMeters(uv00 + vec2(0.0, cell.y));
                float H11 = demMeters(uv00 + cell);
                if (fg.x + fg.y <= 1.0) {
                    // lower-left triangle (H00, H10, H01)
                    meters = H00 + (H10 - H00) * fg.x + (H01 - H00) * fg.y;
                } else {
                    // upper-right triangle (H10, H11, H01)
                    meters = H10 * (1.0 - fg.y) + H01 * (1.0 - fg.x) + H11 * (fg.x + fg.y - 1.0);
                }
            } else {
                meters = demMeters(uv);
            }
            highp float my = uElevationScale.y + pos.y * uElevationScale.z;
            float coshMY = 0.5 * (exp(my) + exp(-my));
            float z = meters * uElevationScale.x * coshMY + uElevationScale.w;
            if (pos.z < -900000.0) {
                // tile skirt bottom vertex: z encodes -1000000 - drop; extrude downwards
                // from the terrain surface to cover cracks between neighbouring tiles
                // that sample different elevation levels
                z += pos.z + 1000000.0;
            }
            return vec3(pos.xy, z);
        }
        #else
        vec3 applyTerrain(vec3 pos) {
            return pos;
        }
        #endif
        // 2D content shaded or shadowed by the terrain needs the elevation uv of the fragment and
        // the local mercator height stretch, so its fragment stage can take the SAME terrain
        // normal the surface takes (see commonFsh). The surface shaders declare these themselves
        // under TERRAIN_LIGHT - declaring them twice in one program is a link error, so this copy
        // exists only for the programs that have no lighting of their own.
        #if defined(TERRAIN) && (defined(TERRAIN_SHADOW) || defined(GEOMETRY_LIGHT)) && !defined(TERRAIN_LIGHT)
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;
        void setTerrainSlopeVaryings(highp vec3 pos) {
            vElevUV = uElevationUV.xy + pos.xy * uElevationUV.zw;
            highp float slopeMY = uElevationScale.y + pos.y * uElevationScale.z;
            vElevCosh = 0.5 * (exp(slopeMY) + exp(-slopeMY));
        }
        #else
        void setTerrainSlopeVaryings(highp vec3 pos) {
        }
        #endif
    )GLSL";

    // The model itself: pure functions of the fog uniforms, always the SDK's, so the tile content,
    // the sky, the background plane and the terrain surface share ONE definition of it.
    //
    // VERBATIM COPY of FogShader::HELPERS in all/native/renderers/utils/FogShader.cpp in the SDK
    // repository, which is the master. A difference between the two is a bug.
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

    // The blends, substituted into commonFsh at $FOG_BLEND$ unless the application supplied its own
    // (GLTileRenderer::setFogShaderSource) - a custom source replaces all three.
    //
    // VERBATIM COPY of FogShader::BUILTIN, same master as above. Colours are PREMULTIPLIED, so the
    // fog colour is premultiplied by this fragment's own alpha; the fog tints what is there rather
    // than adding coverage, so alpha is left alone.
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

    // Marks where fogBlendFsh (or the application's own) goes in commonFsh - it has to come after
    // the helpers it may call and before the applyFog(color) that calls it.
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

        // fogBlend is either the built-in blends or FogOptions::setShaderSource, substituted here
        // so one custom block covers the tile content, the background plane and the sky.
        $FOG_BLEND$

        // Direction and distance WITHOUT a varying - see fogRayVec. An orthographic pass, the drape
        // bake, has gl_FragCoord.w = 1, which is a whole world in internal units, so it never fogs:
        // exactly right, since the bake is flat content that gets fogged later as part of the
        // terrain surface it is painted on.
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
        // The terrain normal at this fragment, for 2D content that is lit or shadowed by the
        // ground and has no lighting of its own. Same 3x3 stencil, same uniforms and same varyings
        // as the surface takes (backgroundFsh), so a road and the ground it lies on get the SAME
        // N.L - and with it the same slope-scaled shadow bias and the same back-face rule. Taken at
        // normal incidence instead, a coplanar receiver gets the minimum bias against a caster it
        // shares its depth with, so half the PCF taps fail and the whole ground shadows itself.
        #if defined(TERRAIN) && (defined(TERRAIN_SHADOW) || defined(GEOMETRY_LIGHT)) && !defined(TERRAIN_LIGHT)
        uniform highp sampler2D uElevationTexture;
        uniform highp vec4 uElevationDecode; // 'vec4' in the vertex stage means highp there
        uniform highp vec4 uElevationTexelSize;
        uniform mediump vec3 uSunDir;          // east, north, up - the frame the tile mesh lives in
        uniform highp vec2 uTerrainSlopeScale; // metres of height -> world units, per elevation-uv unit
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;

        mediump float terrainNdl() {
            highp vec2 duv = uElevationTexelSize.zw;
            highp vec2 ij = vElevUV * uElevationTexelSize.xy;
            highp vec2 cen = floor(ij) + 0.5;
            highp vec2 uv = cen * duv;
            highp float h00 = dot(texture2D(uElevationTexture, uv - duv), uElevationDecode);
            highp float h01 = dot(texture2D(uElevationTexture, uv + vec2(-duv.x, 0.0)), uElevationDecode);
            highp float h02 = dot(texture2D(uElevationTexture, uv + vec2(-duv.x, duv.y)), uElevationDecode);
            highp float h10 = dot(texture2D(uElevationTexture, uv + vec2(0.0, -duv.y)), uElevationDecode);
            highp float h11 = dot(texture2D(uElevationTexture, uv), uElevationDecode);
            highp float h12 = dot(texture2D(uElevationTexture, uv + vec2(0.0, duv.y)), uElevationDecode);
            highp float h20 = dot(texture2D(uElevationTexture, uv + vec2(duv.x, -duv.y)), uElevationDecode);
            highp float h21 = dot(texture2D(uElevationTexture, uv + vec2(duv.x, 0.0)), uElevationDecode);
            highp float h22 = dot(texture2D(uElevationTexture, uv + duv), uElevationDecode);
            highp vec2 f = ij - cen;
            highp float ddxy = (h22 - h20 - h02 + h00) * 0.25;
            highp mat2 curv = mat2(h21 - 2.0 * h11 + h01, ddxy, ddxy, h12 - 2.0 * h11 + h10);
            highp vec2 grad0 = vec2(h21 - h01, h12 - h10) * 0.5;
            highp vec2 grad = grad0 + curv * f; // metres per texel
            highp float dx = grad.x * uTerrainSlopeScale.x * vElevCosh / duv.x;
            highp float dy = grad.y * uTerrainSlopeScale.y * vElevCosh / duv.y;
            return max(0.0, dot(normalize(vec3(-dx, -dy, 1.0)), uSunDir));
        }
        #else
        mediump float terrainNdl() {
            return 1.0;
        }
        #endif
        #if defined(TERRAIN_SHADOW) && defined(SHADOW_MASK_IN)
        // The terrain's shadow, already resolved for this screen pixel by the mask pass. One fetch
        // instead of a cascade choice, a matrix, derivatives and four taps - and the ground is
        // covered twice over, by the drape and by the paint on top of it, so this is paid twice.
        uniform sampler2D uShadowMask;
        uniform highp vec2 uShadowMaskScale; // 1 / screen size, whatever resolution the mask is at
        mediump float shadowFactorScreen() {
            return texture2D(uShadowMask, gl_FragCoord.xy * uShadowMaskScale).r;
        }
        mediump float shadowFactorSlope(mediump float ndl) {
            return shadowFactorScreen();
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
        // A COMPARISON sampler: one fetch does four depth compares and returns their bilinear
        // average, in the texture unit. Four of these are sixteen samples for what the packed path
        // paid for four unfiltered ones. ESSL 3.00 only - see SHADOW_DEPTH_TEXTURE.
        uniform highp sampler2DShadow uShadowTexture;
        #else
        uniform sampler2D uShadowTexture;
        #endif
        uniform mediump vec4 uShadowParams; // x = 1/mapSize within one cascade, y = strength, z = PCF radius in texels, w = 1/cascade count
        uniform mediump vec4 uShadowBias;   // normalised depth bias, per cascade
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

        // True when a light-space position does not land inside its cascade's page, with room for
        // the PCF kernel: such a fragment has to fall back to the next, coarser cascade.
        bool outsideShadowPage(highp vec3 pos, mediump float margin) {
            return pos.x < margin || pos.x > 1.0 - margin || pos.y < margin || pos.y > 1.0 - margin || pos.z < 0.0 || pos.z > 1.0;
        }

        // The caster pass packs window-space depth into RGB; unpack and compare with a slope
        // independent constant plus the caller's bias. The uv is in ATLAS space: the cascades are
        // pages of one texture, side by side, near page first.
        // One tap: is this reference depth in front of what the map holds here? 1 = lit.
        mediump float shadowTap(highp vec2 uv, highp float ref) {
        #if defined(SHADOW_HW)
            return texture(uShadowTexture, vec3(uv, ref));
        #elif defined(SHADOW_DEPTH_TEXTURE)
            // The depth buffer itself: no packing to undo, and the caster pass wrote no colour.
            return ref <= texture2D(uShadowTexture, uv).r ? 1.0 : 0.0;
        #else
            highp vec4 enc = texture2D(uShadowTexture, uv);
            return ref <= dot(enc.rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0)) ? 1.0 : 0.0;
        #endif
        }
        // 3x3 PCF over a radius in shadow-map texels. One shadow texel covers many metres of
        // ground, so a single tap gives hard stair-stepped edges; averaging over a small kernel is
        // what makes a finite-resolution shadow map look like a shadow rather than a mask.
        // ndl scales the bias with the angle between the surface and the light: at a grazing
        // angle one shadow texel spans a large depth range, and a constant bias cannot cover it.
        // Left constant, the residual self-shadowing lands in bands of constant height - which on
        // a hillside reads as ripples following the contour lines.
        mediump float shadowFactorSlope(mediump float ndl) {
            // Cascades, near page first: a fragment takes the sharpest page it falls inside. The
            // margin keeps the PCF taps of the chosen page off its border, where they would read
            // the neighbouring cascade's texels.
            mediump float margin = uShadowParams.x * (uShadowParams.z + 1.0);
            highp vec3 pos = vShadowPos0;
            mediump float page = 0.0;
            mediump float bias = uShadowBias.x;
        #if SHADOW_CASCADES >= 2
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos1;
                page = 1.0;
                bias = uShadowBias.y;
            }
        #endif
        #if SHADOW_CASCADES >= 3
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos2;
                page = 2.0;
                bias = uShadowBias.z;
            }
        #endif
        #if SHADOW_CASCADES >= 4
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos3;
                page = 3.0;
                bias = uShadowBias.w;
            }
        #endif
            // Derivatives are taken before any early return: a fragment that leaves the function
            // early would leave its quad neighbours with an undefined gradient.
            highp vec2 dzduv = vec2(0.0);
            highp float o = uShadowParams.x * uShadowParams.z;
            highp float ref = pos.z;
        #ifdef DERIVATIVES
            // Receiver-plane depth bias: how the receiver's own depth changes per unit of shadow
            // uv, from screen-space derivatives. Each PCF tap then compares against the receiver's
            // PLANE instead of against one point on it. Without it every off-centre tap needs the
            // constant bias to cover a whole texel of slope, which is why the acne only cleared at
            // a bias large enough to detach the shadows.
            highp vec3 dpdx = dFdx(pos);
            highp vec3 dpdy = dFdy(pos);
            highp float det = dpdx.x * dpdy.y - dpdx.y * dpdy.x;
            if (abs(det) > 1.0e-12) {
                dzduv.x = ( dpdy.y * dpdx.z - dpdx.y * dpdy.z) / det;
                dzduv.y = (-dpdy.x * dpdx.z + dpdx.x * dpdy.z) / det;
                // A near-silhouette texel has an unbounded gradient; clamp it so it cannot invert
                // the comparison and punch holes in the shadow. The limit is a cap on how much
                // depth the receiver plane may rise over ONE texel, as a fraction of the box
                // depth. Derived from the PCF radius instead, it allowed a single texel to be
                // worth half the whole light box - which does not bias the comparison so much as
                // delete it, and is what left holes in the shadow on steep ground.
                highp float limit = 0.02 / max(1.0e-6, uShadowParams.x);
                dzduv = clamp(dzduv, vec2(-limit), vec2(limit));
                // The stored depth belongs to the TEXEL CENTRE, up to half a texel away from this
                // fragment; on a slope lit at a grazing angle that half texel is metres of height,
                // so the surface shadows itself in regular stripes - the bands seen inside a long
                // shadow stretched downhill. Subtracting the receiver plane's own rise over half a
                // texel makes the bias exactly as large as the local slope demands and no larger,
                // where a constant big enough for the worst slope would detach every shadow.
                ref -= 0.5 * uShadowParams.x * (abs(dzduv.x) + abs(dzduv.y));
            }
        #endif
            mediump float facing = smoothstep(0.0, 0.15, ndl);
            if (pos.x < 0.0 || pos.x > 1.0 || pos.y < 0.0 || pos.y > 1.0 || pos.z < 0.0 || pos.z > 1.0) {
                // Outside every cascade: unshadowed rather than black - but the back-face rule
                // below still applies, or the ground would brighten along a hard ring exactly where
                // the last cascade ends.
                return mix(1.0, facing, uShadowParams.y);
            }
            // A surface turned away from the sun is in its own shadow whatever the map says, so the
            // taps cannot change the answer: skipping them there is free. It is worth its own branch
            // because that is a third of a hillside and about half of every building wall - the 3D
            // pass is where this pays most.
            if (facing <= 0.0) {
                return mix(1.0, 0.0, uShadowParams.y);
            }
            // The constant bias grows as the surface turns away from the light, but only up to a
            // point: divided by N.L it runs away exactly where the shadows are - the slopes facing
            // away from the sun - and lifts the reference depth in front of every caster there,
            // which is the second source of holes. Beyond this the back-face rule below takes over.
            ref -= bias / max(0.5, ndl);
            // Page space -> atlas space. The offsets stay in page space so the kernel is square in
            // the map, and the bias terms above stay in the units the derivatives produced.
            highp vec2 atlasScale = vec2(uShadowParams.w, 1.0);
            highp vec2 atlasBase = vec2(page * uShadowParams.w, 0.0);
            mediump float lit = 0.0;
        #ifdef SHADOW_SINGLE_TAP
            lit = shadowTap(atlasBase + pos.xy * atlasScale, ref);
        #else
            // Four taps on the diagonals rather than a 3x3: the centre and the edge midpoints of a
            // 3x3 carry almost the same answer as the corners, and the taps are the second cost of
            // the lookup after the varyings. The spacing keeps the same penumbra width.
            highp float d = o * 0.75;
            for (int j = 0; j < 2; j++) {
                for (int i = 0; i < 2; i++) {
                    highp vec2 offset = vec2(float(i) * 2.0 - 1.0, float(j) * 2.0 - 1.0) * d;
                    lit += shadowTap(atlasBase + (pos.xy + offset) * atlasScale, ref + dot(offset, dzduv));
                }
            }
            lit *= 0.25;
        #endif
            // The outermost cascade ends somewhere - at the shadow distance, or at the point where
            // covering more ground would only coarsen every texel. Ending it abruptly draws a line
            // across the terrain, so the shadow fades out over the outer margin of the LAST page.
            // Earlier pages must not fade: a fragment leaving one of those is picked up by the next
            // cascade, and fading there would thin the shadow along every cascade boundary.
            mediump float lastPage = 1.0 / uShadowParams.w - 1.0;
            if (page >= lastPage - 0.5) {
                mediump float edge = min(min(pos.x, 1.0 - pos.x), min(pos.y, 1.0 - pos.y));
                lit = mix(1.0, lit, smoothstep(0.0, 0.08, edge));
            }
            // A surface turned away from the sun is in its own shadow whatever the map says, and
            // the map cannot say anything useful there anyway: its texels are seen edge-on, so the
            // depth stored for one covers the whole face. Shadowing those outright is both correct
            // and what makes it safe to keep the bias small everywhere else.
            lit = min(lit, facing);
            return mix(1.0, lit, uShadowParams.y);
        }
        mediump float shadowFactor() {
            return shadowFactorSlope(1.0);
        }
        #endif
        // 2D content standing ON the ground, drawn in the 3D scene instead of baked into the drape:
        // it takes the ground's sun AND the ground's shadow, from the terrain normal under it.
        // Without the sun a no-drape layer (contours) kept its full style colour while everything
        // draped around it was lit - GEOMETRY_LIGHT is what closes that.
        #if defined(TERRAIN) && defined(GEOMETRY_LIGHT) && !defined(TERRAIN_LIGHT)
        uniform lowp vec4 uSunColor;        // rgb = colour, a = unused
        uniform lowp vec4 uAmbientColor;    // rgb = colour, a = unused
        uniform mediump vec2 uLightParams;  // x = sun intensity, y = ambient intensity
        lowp vec4 applyTerrainShading(lowp vec4 color) {
            mediump float ndl = terrainNdl();
            // The same normalised Lambert the draped surface uses (backgroundFsh): ambient is the
            // floor, the sun fills the remaining headroom. The colour is premultiplied, so scaling
            // rgb alone is a valid tint and the clamp keeps rgb <= a.
            mediump vec3 lit = uAmbientColor.rgb * uLightParams.y + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
        #if defined(TERRAIN_SHADOW) && defined(SHADOW_MASK_IN)
            lit *= shadowFactorScreen();
        #elif defined(TERRAIN_SHADOW)
            lit *= shadowFactorSlope(ndl);
        #endif
            return vec4(min(color.rgb * lit, vec3(color.a)), color.a);
        }
        #elif defined(TERRAIN_SHADOW) && defined(SHADOW_MASK_IN)
        // The mask already holds this pixel's terrain shadow; the analytic form would also have to
        // build the normal first, which is a 3x3 stencil over the elevation texture.
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
    )GLSL";

    // Per-fragment terrain lighting needs the elevation uv of the fragment and the local mercator
    // height stretch; both are linear in the vertex position, so they interpolate. Prepended to
    // every vertex shader whose fragment stage takes the terrain normal - commonVsh cannot hold it,
    // the terrain-paint path declares the same varyings itself.
    static const std::string terrainLightVsh = R"GLSL(
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;
        void setTerrainLightVaryings(highp vec3 pos) {
            vElevUV = uElevationUV.xy + pos.xy * uElevationUV.zw;
            highp float lightMY = uElevationScale.y + pos.y * uElevationScale.z;
            vElevCosh = 0.5 * (exp(lightMY) + exp(-lightMY));
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
        // xy = uv offset, zw = uv scale. Identity for a tile drawn with its own drape texture;
        // a sub-rect when the tile is standing in on an ancestor's texture because its own is
        // not baked yet.
        uniform highp vec4 uDrapeUVTransform;
        varying highp_opt vec2 vDrapeUV;
        #endif

        void main(void) {
        #ifdef PATTERN
            vUV = aVertexUV * uUVScale;
        #endif
            setTerrainLightVaryings(aVertexPosition);
        #ifdef DRAPE
            // The regular-grid surface vertex xy is the tile-local [0,1] parametrization;
            // it is exactly the uv the tile's fills were baked into the drape texture with.
            // If fills appear vertically mirrored on device, flip to vec2(x, 1.0 - y).
            vDrapeUV = uDrapeUVTransform.xy + aVertexPosition.xy * uDrapeUVTransform.zw;
        #endif
        #ifdef LIGHTING_VSH
            vColor = applyLighting(vec4(1.0, 1.0, 1.0, 1.0), aVertexNormal);
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            highp vec3 terrainPos = applyTerrain(aVertexPosition);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        }
    )GLSL";

    // Caster pass: the light-space depth of the terrain surface, packed into RGB so no depth
    // texture extension is needed. The vertex shader is backgroundVsh with the light matrix in
    // uMVPMatrix, so the caster geometry is bit-identical to the geometry that is drawn.
    static const std::string shadowCasterFsh = R"GLSL(
        void main(void) {
            highp float depth = gl_FragCoord.z;
            highp vec3 enc = vec3(1.0, 255.0, 65025.0) * depth; // 'packed' is a reserved word
            enc = fract(enc);
            enc -= enc.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
            glFragColor = vec4(enc, 1.0);
        }
    )GLSL";

    // The terrain normal at this fragment and the sun that lights it. Prepended to every fragment
    // shader that lights draped content - commonFsh cannot hold it, the terrain-paint path declares
    // the same names for itself and one name declared twice in a stage does not compile.
    static const std::string terrainLightFsh = R"GLSL(
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
        // Precision qualifiers must match the vertex-stage declarations exactly, or the
        // program fails to LINK (same name, different precision is an error in GLSL ES 1.00).
        uniform highp sampler2D uElevationTexture;
        uniform highp vec4 uElevationDecode; // 'vec4' in the vertex stage means highp there
        uniform highp vec4 uElevationTexelSize;
        uniform mediump vec3 uSunDir;         // east, north, up - the same frame the tile mesh lives in
        uniform lowp vec4 uSunColor;          // rgb = colour, a = unused
        uniform lowp vec4 uAmbientColor;      // rgb = colour, a = unused
        uniform mediump vec2 uLightParams;    // x = sun intensity, y = ambient intensity
        uniform highp vec2 uTerrainSlopeScale; // metres of height -> world units, per elevation-uv unit
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;

        // The DEM gradient, as tangram's `normal` block takes it - see terrainSampleDem below for
        // why the stencil is quadratic rather than a central difference. The surface is displaced by
        // exactly this height field in the vertex stage, so this is the normal of what is drawn.
        // No decode offset needed (only differences are used); highp because heights reach several
        // thousand metres and mediump would leave the differences as rounding noise.
        mediump vec3 terrainNormal() {
            highp vec2 duv = uElevationTexelSize.zw;
            highp vec2 ij = vElevUV * uElevationTexelSize.xy;
            highp vec2 cen = floor(ij) + 0.5;
            highp vec2 uv = cen * duv;
            highp float h00 = dot(texture2D(uElevationTexture, uv - duv), uElevationDecode);
            highp float h01 = dot(texture2D(uElevationTexture, uv + vec2(-duv.x, 0.0)), uElevationDecode);
            highp float h02 = dot(texture2D(uElevationTexture, uv + vec2(-duv.x, duv.y)), uElevationDecode);
            highp float h10 = dot(texture2D(uElevationTexture, uv + vec2(0.0, -duv.y)), uElevationDecode);
            highp float h11 = dot(texture2D(uElevationTexture, uv), uElevationDecode);
            highp float h12 = dot(texture2D(uElevationTexture, uv + vec2(0.0, duv.y)), uElevationDecode);
            highp float h20 = dot(texture2D(uElevationTexture, uv + vec2(duv.x, -duv.y)), uElevationDecode);
            highp float h21 = dot(texture2D(uElevationTexture, uv + vec2(duv.x, 0.0)), uElevationDecode);
            highp float h22 = dot(texture2D(uElevationTexture, uv + duv), uElevationDecode);
            highp vec2 f = ij - cen;
            highp float ddxy = (h22 - h20 - h02 + h00) * 0.25;
            highp mat2 curv = mat2(h21 - 2.0 * h11 + h01, ddxy, ddxy, h12 - 2.0 * h11 + h10);
            highp vec2 grad0 = vec2(h21 - h01, h12 - h10) * 0.5;
            highp vec2 grad = grad0 + curv * f; // metres per texel
            highp float dx = grad.x * uTerrainSlopeScale.x * vElevCosh / duv.x;
            highp float dy = grad.y * uTerrainSlopeScale.y * vElevCosh / duv.y;
            return normalize(vec3(-dx, -dy, 1.0));
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
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
            // The draped colour is premultiplied, so scaling rgb alone is a valid tint; clamp
            // back to alpha so an intensity above 1 cannot break premultiplication.
            mediump float ndl = max(0.0, dot(terrainNormal(), uSunDir));
        #if defined(SHADOW_MASK_OUT) && defined(TERRAIN_SHADOW)
            // The mask pass: this draw exists only to resolve the terrain's shadow for the pixel,
            // from the same geometry, the same elevation and the same normal the surface itself
            // will use, so the value the surface samples back is the one it would have computed.
            glFragColor = vec4(vec3(shadowFactorSlope(ndl)), 1.0);
            return;
        #endif
            // Normalised Lambert: ambient is the floor, the sun fills the REMAINING headroom, so
            // a surface facing the sun lands at 1 instead of ambient+1. Adding them blows the
            // ground out to white at a high sun, and a clipped highlight cannot show a shadow.
            mediump vec3 lit = uAmbientColor.rgb * uLightParams.y + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
        #ifdef TERRAIN_SHADOW
            // The shadow multiplies the FINAL colour, exactly as it does on the 3D extrusions.
            // Folding it into N.L instead made it vanish at ambient 1 (where N.L has no weight
            // left), so ground and buildings disagreed about what a shadow is. Shadow depth is
            // the strength parameter's job, not the ambient level's.
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
            vColor = applyLighting(vec4(1.0, 1.0, 1.0, 1.0), aVertexNormal);
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
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
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
            // The draped colour is premultiplied, so scaling rgb alone is a valid tint; clamp
            // back to alpha so an intensity above 1 cannot break premultiplication.
            mediump float ndl = max(0.0, dot(terrainNormal(), uSunDir));
            // Normalised Lambert: ambient is the floor, the sun fills the REMAINING headroom, so
            // a surface facing the sun lands at 1 instead of ambient+1. Adding them blows the
            // ground out to white at a high sun, and a clipped highlight cannot show a shadow.
            mediump vec3 lit = uAmbientColor.rgb * uLightParams.y + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
        #ifdef TERRAIN_SHADOW
            // The shadow multiplies the FINAL colour, exactly as it does on the 3D extrusions.
            // Folding it into N.L instead made it vanish at ambient 1 (where N.L has no weight
            // left), so ground and buildings disagreed about what a shadow is. Shadow depth is
            // the strength parameter's job, not the ambient level's.
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
            vNormal = aVertexNormal;
            vBinormal = aVertexBinormal;
        #endif
            highp vec3 terrainPos = applyTerrain(aVertexPosition);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        }
    )GLSL";

    // Prepended to the normal-map lighting shader (custom or built-in) so both the injected shader
    // and the base fragment shader can read the DEM. Declares the shared samplers/uniforms and the
    // elevation helpers a CUSTOM shader can call: getElevation() (meters at this fragment),
    // getMapZoom() (fractional map zoom) and sampleElevation(uv). Only used in the normal-map path.
    static const std::string normalmapCustomPrelude = R"GLSL(
        uniform sampler2D uBitmap;
        uniform highp_opt vec4 uUVScale;
        varying highp_opt vec2 vUV;
        // Elevation-encoded normal map (opt-in): R,G hold normal.xy (z is reconstructed), B,A hold a
        // 16-bit elevation and contrast comes from a uniform. u_elevationEncoded <= 0.5 keeps the
        // classic RGB=normal, A=contrast behaviour byte for byte.
        uniform mediump float u_elevationEncoded;
        uniform highp_opt vec2 u_elevationDecode; // meters = elev16 * x + y
        uniform lowp float u_contrast;            // replaces the alpha channel when elevation-encoded
        uniform highp_opt float u_zoom;           // current fractional map zoom (for per-zoom custom shaders)

        highp_opt float decodeElevation(lowp vec4 s) {
            return (s.b * 255.0 * 256.0 + s.a * 255.0) * u_elevationDecode.x + u_elevationDecode.y;
        }
        // Manual bilinear of the decoded METERS (decode each texel first, then blend) so the 16-bit
        // hi/lo byte split can not wrap under texture filtering and produce false contour lines.
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
        // Convenience for custom normal-map / custom raster shaders.
        highp_opt float getElevation() { return sampleElevation(vUV); }
        highp_opt float getMapZoom() { return u_zoom; }
        // Raw texel of the source tile at this fragment (e.g. the untouched RGB(A) raster for a
        // CustomRasterTileLayer filter shader).
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
        #if defined(LIGHTING_FSH)
            mediump vec3 tspaceNormal;
            if (u_elevationEncoded > 0.5) {
                mediump vec2 nxy = packedNormalAlpha.xy * 2.0 - vec2(1.0);
                tspaceNormal = vec3(nxy, sqrt(max(0.0, 1.0 - dot(nxy, nxy))));
                color = vec4(u_contrast); // contrast is a uniform; the alpha channel now holds elevation
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
                // Screen-width anti-aliased contour lines (tangram/ascendmaps style): distance to the
                // nearest contour in meters, divided by the per-pixel elevation change (fwidth).
                highp_opt float e = sampleElevation(vUV);
                highp_opt float frac = fract(e / u_contourInterval);
                highp_opt float distM = min(frac, 1.0 - frac) * u_contourInterval;
                mediump float px = distM / max(fwidth(e), 1e-4);
                mediump float cov = clamp(u_contourWidth - px + 0.5, 0.0, 1.0) * u_contourColor.a;
                // premultiplied over-composite (applyLighting returns premultiplied rgb)
                shade.rgb = u_contourColor.rgb * cov + shade.rgb * (1.0 - cov);
                shade.a = cov + shade.a * (1.0 - cov);
            }
            glFragColor = applyFog(shade * uOpacity);
        #else
            glFragColor = applyFog(color * uOpacity);
        #endif
        }
    )GLSL";

    // Terrain paint: hillshading computed directly from the shared terrain elevation texture,
    // as one quad per tile, instead of from a per-tile normal map raster of its own. There is
    // no tile set behind it - the DEM the 3D terrain already has bound IS the data - so the
    // paint costs one draw where a hillshade layer costs a tile set, a decode, a normal map
    // and a surface pass. The lighting itself is unchanged: the same normal-map lighting
    // shader (built-in or custom) is injected and fed a normal rebuilt from the DEM gradient.
    static const std::string terrainPaintPrelude = R"GLSL(
        uniform highp sampler2D uElevationTexture;
        // Precision qualifiers must match the vertex-stage declarations exactly, or the program
        // fails to LINK (same name, different precision is an error in GLSL ES 1.00).
        uniform highp vec4 uElevationDecode;
        uniform highp float uElevationOffset;   // the decode's constant term (the texture carries no spare channel for it)
        uniform highp vec4 uElevationTexelSize; // xy: texture size in texels, zw: 1 / size
        uniform highp vec2 uPaintSlopeScale;    // metres per texel -> the dimensionless slope the hillshade algorithms expect (height scale folded in)
        uniform mediump vec4 uPaintParams;      // x = contrast, y = opacity, zw reserved
        uniform highp_opt float u_zoom;         // current fractional map zoom, for per-zoom custom shaders
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;

        highp float sampleElevation(highp vec2 uv) {
            return dot(texture2D(uElevationTexture, uv), uElevationDecode) + uElevationOffset;
        }

        // Tangram's DEM sample, ported whole from the `normal` block of hillshade.yaml: ONE 3x3
        // stencil at TEXEL CENTRES (exact whatever the hardware filter does) plus a quadratic Taylor
        // expansion, grad = grad0 + curv*f and elev = h11 + f.grad0 + 0.5*f.curv.f. The gradient is
        // the point - a central difference at a fixed step is CONSTANT over a texel cell and breaks
        // the shading into texel-sized facets (the "pixelated hillshade" close up); the 9 taps
        // replace the 8 the Sobel took. highp because differences of thousands of metres in mediump
        // are rounding noise. Our textures carry a 1-texel neighbour border, so unlike tangram no
        // edge extrapolation is needed. Taken ONCE per fragment in terrainPaintPrepare() and read
        // from here by the slope, the sun normal, the contours and a custom getElevation().
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

        // The same contract the normal-map path offers custom shaders, backed by the shared DEM.
        highp float getElevation() { return gTerrainElev; }
        highp float getMapZoom() { return u_zoom; }
        lowp vec4 getRawColor() { return texture2D(uElevationTexture, vElevUV); }

        // The slope handed to the hillshade algorithms, in the (dh/dEast, -dh/dNorth) convention
        // the normal map encoded - v grows NORTH in the elevation texture. The mercator 1/cos
        // stretch is per fragment; everything constant over the tile is in uPaintSlopeScale.
        mediump vec2 terrainPaintDeriv() {
            return vec2(gTerrainGrad.x, -gTerrainGrad.y) * vElevCosh * uPaintSlopeScale;
        }
    )GLSL";

    static const std::string terrainPaintVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        uniform mat4 uMVPMatrix;
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;

        void main(void) {
            vElevUV = uElevationUV.xy + aVertexPosition.xy * uElevationUV.zw;
            highp float my = uElevationScale.y + aVertexPosition.y * uElevationScale.z;
            vElevCosh = 0.5 * (exp(my) + exp(-my));
        #ifdef PAINT_SURFACE
            // Drawn as the terrain surface itself, displaced by the DEM - tangram's model, where
            // the hillshade is a draw on the tile's own terrain mesh rather than a texture baked
            // for it. Same grid VBO as every other surface draw, per-tile uniforms only.
            highp vec3 terrainPos = applyTerrain(aVertexPosition);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        #else
            // The drape bake is flat and orthographic: the quad is the tile-local unit square and
            // the paint is a function of the DEM alone, so no displacement is needed there.
            gl_Position = uMVPMatrix * vec4(aVertexPosition.xy, 0.0, 1.0);
        #endif
        }
    )GLSL";

    static const std::string terrainPaintFsh = R"GLSL(
        #ifdef GROUND_BASE
        // The paint IS the ground in this mode, so it carries the ground's own colour underneath
        // its shading and there is no separate fill draw - tangram's arrangement, where the terrain
        // raster's `color` block starts from a base colour (res/scenes/hillshade.yaml:
        // `base_color = vec4(0.88, 0.88, 0.88, 1.0)` under TANGRAM_TERRAIN_3D) and shades THAT.
        uniform lowp vec4 uGroundColor;
        #endif
        #ifdef PAINT_SURFACE
        // Contour lines as a fragment block on the terrain draw, which is where tangram puts them
        // (res/scenes/hillshade.yaml computes hillshade, hypsometric tint and contours in the
        // `color` block of the raster style that IS the terrain surface). Same uniforms and the
        // same screen-width anti-aliasing as the normal-map path, so a layer gets identical
        // contours whether it shades a per-tile normal map or the shared DEM - the paint used to
        // switch itself off when contours were asked for, precisely because it lacked this.
        // Declared here and NOT in the prelude: normalmapFsh is not part of this program.
        uniform lowp vec4 u_contourColor;
        uniform highp_opt float u_contourInterval; // metres between contour lines; <= 0 disables them
        uniform mediump float u_contourWidth;      // contour half-width in screen pixels
        #endif
        #if defined(PAINT_SURFACE) && defined(TERRAIN_LIGHT)
        // Drawn as the terrain surface, so it takes the sun and the shadow map the surface takes -
        // otherwise the paint covers a lit, shadowed ground with an unlit copy of it and the
        // shadows simply disappear under the hillshade. The uniforms the paint prelude already
        // declares (elevation sampler, texel size, vElevUV) are NOT redeclared here: a second
        // declaration of the same name is a link error.
        uniform mediump vec3 uSunDir;          // east, north, up - the frame the tile mesh lives in
        uniform lowp vec4 uSunColor;           // rgb = colour, a = unused
        uniform lowp vec4 uAmbientColor;       // rgb = colour, a = unused
        uniform mediump vec2 uLightParams;     // x = sun intensity, y = ambient intensity
        uniform highp vec2 uTerrainSlopeScale; // metres of height -> world units, per elevation-uv unit

        // The GEOMETRIC normal, from the true slope scale - not terrainPaintDeriv(), whose scale
        // carries the hillshade's own height scale and relief boost and would light the ground
        // several times too hard.
        mediump vec3 terrainSurfaceNormal() {
            // Same stencil as the hillshade (terrainPaintSample): the gradient interpolated
            // through the curvature, not a per-texel-constant central difference, so the sun
            // lighting does not facet at texel boundaries either.
            highp vec2 st = uElevationTexelSize.zw;
            highp float dx = gTerrainGrad.x * uTerrainSlopeScale.x * vElevCosh / st.x;
            highp float dy = gTerrainGrad.y * uTerrainSlopeScale.y * vElevCosh / st.y;
            return normalize(vec3(-dx, -dy, 1.0));
        }
        #endif

        void main(void) {
            terrainPaintPrepare(); // the one DEM stencil this fragment takes
            mediump vec2 deriv = terrainPaintDeriv();
            // applyLighting() recovers the gradient as vec2(-n.x, n.y)/n.z, so hand it a normal
            // that reproduces this deriv exactly. The surface normal is the flat one: the paint
            // is baked into the tile texture, and the terrain surface tilts it afterwards.
            mediump vec3 normal = normalize(vec3(-deriv.x, deriv.y, 1.0));
            lowp vec4 color = applyLighting(vec4(uPaintParams.x), normal, vec3(0.0, 0.0, 1.0), 0.0);
        #ifdef PAINT_SURFACE
            color = color * uPaintParams.y;
        #if defined(TERRAIN_LIGHT)
            // Same normalised Lambert and the same final-colour shadow multiply as the terrain
            // surface itself (backgroundFsh), so ground and paint agree about what a shadow is.
            mediump float ndl = max(0.0, dot(terrainSurfaceNormal(), uSunDir));
            mediump vec3 lit = uAmbientColor.rgb * uLightParams.y + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
        #ifdef TERRAIN_SHADOW
            lit *= shadowFactorSlope(ndl);
        #endif
            color = vec4(min(color.rgb * lit, vec3(color.a)), color.a);
        #endif
            if (u_contourInterval > 0.0) {
                // Distance to the nearest contour in metres, divided by the per-pixel elevation
                // change, gives a screen-space width that stays constant as the ground tilts away.
                // Composited OVER the shaded ground, premultiplied - the same order and the same
                // result as normalmapFsh, so switching a layer to the paint does not move the lines.
                highp_opt float e = getElevation(); // the quadratic reconstruction: contours that
                                                    // do not kink at every texel boundary
                highp_opt float frac = fract(e / u_contourInterval);
                highp_opt float distM = min(frac, 1.0 - frac) * u_contourInterval;
                mediump float px = distM / max(fwidth(e), 1e-4);
                mediump float cov = clamp(u_contourWidth - px + 0.5, 0.0, 1.0) * u_contourColor.a;
                color.rgb = u_contourColor.rgb * cov + color.rgb * (1.0 - cov);
                color.a = cov + color.a * (1.0 - cov);
            }
        #ifdef GROUND_BASE
            // Over the ground colour, premultiplied - the shade already is. One opaque draw per
            // tile then covers what the fill pass used to draw underneath it.
            color = vec4(color.rgb + uGroundColor.rgb * uGroundColor.a * (1.0 - color.a),
                         color.a + uGroundColor.a * (1.0 - color.a));
        #endif
            glFragColor = applyFog(color); // drawn in the scene, so it fogs with it
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
        // Glyph quad corner, relative to the label anchor in aVertexPosition. Already scaled.
        // aVertexAttribs[3] says how to orient it: 0 = a world offset ready to add (labels
        // whose axes come from their placement, computed once on the CPU), 1 = x/y on the
        // camera axes. Resolving the camera case here rather than on the CPU takes the camera
        // out of the vertex data, which is what lets a label batch be uploaded once instead
        // of once per frame.
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
        // x = half the occluder square, in uv; y = depth offset, NDC; z = the opacity an occluded
        // label keeps; w = 1/(the ramp the comparison is softened over).
        uniform vec4 uLabelOcclusionParams;
        #endif
        uniform vec2 uUVScale;
        uniform float uSDFRamp;
        uniform vec4 uColorTable[16];
        uniform float uWidthTable[16];
        uniform float uStrokeWidthTable[16];
        varying lowp vec4 vColor;
        varying highp_opt vec2 vUV;
        varying mediump vec4 vAttribs;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float size = uWidthTable[styleIndex];
            float opacity = aVertexAttribs[2] * (1.0 / 127.0);
            vec4 color = aVertexAttribs[1] > 1.0 ? vec4(1.0, 1.0, 1.0, 1.0) : uColorTable[styleIndex];
            vUV = aVertexUV * uUVScale;
            // [1] is the halo width in SCREEN PIXELS, [3] the antialias ramp - one screen pixel of
            // signed distance. The fragment shader measures the halo in ramps, so both are the
            // same unit and a halo is as wide as the style asks whatever raster size the label
            // landed on (the ramp is re-measured per fragment when derivatives are available).
            vAttribs = vec4(aVertexAttribs[1], uStrokeWidthTable[styleIndex], 0.0, uSDFRamp / size);
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal) * opacity;
        #else
            vColor = color * opacity;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            vec3 offset = aVertexAttribs[3] > 0.5
                ? uLabelAxisX * aVertexOffset.x + uLabelAxisY * aVertexOffset.y
                : aVertexOffset;
        #ifdef LABEL_OCCLUSION
            // Is the ANCHOR behind a 3D occluder? Four taps around it, each a soft comparison,
            // averaged - so a label goes out as it slides behind a building rather than popping,
            // and one texel of a half-resolution buffer cannot decide it alone. Per LABEL, not per
            // fragment: the glyph run is never cut in half by a wall crossing it.
            highp vec4 anchorClip = uMVPMatrix * vec4(aVertexPosition, 1.0);
            if (anchorClip.w > 0.0) {
                highp vec2 anchorUV = anchorClip.xy / anchorClip.w * 0.5 + 0.5;
                highp float anchorDepth = anchorClip.z / anchorClip.w * 0.5 + 0.5 + uLabelOcclusionParams.y;
                highp vec2 d = vec2(uLabelOcclusionParams.x);
                // The occluders arrive with their window depth PACKED into rgb (the shadow
                // caster's encoding), so an empty texel - white - decodes past 1 and reads as
                // nothing in front.
                highp vec3 unpack = vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0);
                highp vec4 taps = vec4(
                    dot(texture2D(uLabelOcclusionTex, anchorUV + vec2( d.x,  d.y)).rgb, unpack),
                    dot(texture2D(uLabelOcclusionTex, anchorUV + vec2(-d.x,  d.y)).rgb, unpack),
                    dot(texture2D(uLabelOcclusionTex, anchorUV + vec2( d.x, -d.y)).rgb, unpack),
                    dot(texture2D(uLabelOcclusionTex, anchorUV + vec2(-d.x, -d.y)).rgb, unpack));
                lowp float visible = dot(vec4(0.25), clamp((taps - vec4(anchorDepth)) * uLabelOcclusionParams.w, 0.0, 1.0));
                vColor *= mix(uLabelOcclusionParams.z, 1.0, visible); // premultiplied, so one scalar is enough
            }
        #endif
            gl_Position = uMVPMatrix * vec4(aVertexPosition + offset, 1.0);
        }
    )GLSL";

    static const std::string labelFsh = R"GLSL(
        uniform sampler2D uBitmap;
        varying lowp vec4 vColor;
        varying highp_opt vec2 vUV;
        varying mediump vec4 vAttribs;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            // The signed distance is read at mediump on purpose: at lowp the 8-bit field is
            // quantized coarser than the one-pixel ramp below, which shows up as banded glyph
            // edges - and the derivative taken from it would be noise.
            mediump vec4 color = texture2D(uBitmap, vUV);
            if (vAttribs[0] > 0.0) {
                color = color * vColor;
            } else {
        #ifdef DERIVATIVES
                // The gradient of the field itself is the width of one screen pixel expressed in
                // the field's own units - it needs no constant and, unlike the per-batch value
                // below, it follows a label the perspective magnifies (one lying on the ground
                // under a tilt is drawn at a scale that varies over the label).
                mediump float size = max(length(vec2(dFdx(color.r), dFdy(color.r))), 0.00001);
        #else
                mediump float size = vAttribs[3];
        #endif
                // The ramp is centred on the outline, and the halo pushes that centre outward by
                // its own width in screen pixels - 'size' being exactly one of those.
                float offset = 0.5 * (1.0 - size * (1.0 + 2.0 * vAttribs[1]));
                color = clamp((color.r - offset) / size, 0.0, 1.0) * vColor;
            }
        #ifdef LIGHTING_FSH
            color = applyLighting(color, normalize(vNormal));
        #endif
            // Fogged like everything else, and then faded OUT once the haze is solid: a label with
            // no map left under it reads as floating text (mapbox clips its symbols at the same
            // 0.9). fogLabelFade is 1 without fog.
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
            float offset = 0.5 - 0.5 * uSDFScale / size * (1.0 + uStrokeWidthTable[styleIndex]);
        #else
            float offset = 0.5 - 0.5 * uSDFScale / size;
        #endif
            vAttribs = vec4(aVertexAttribs[1], 0.0, offset, size / uSDFScale);
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            // Sample the terrain at the EXTRUDED corner, exactly as the line shader does: a quad
            // placed at the anchor's height and then offset sideways is a flat plate, and on a
            // slope its uphill half sits under the ground, where the depth test against the terrain
            // surface eats it. A glyph quad of clipped text is wide enough for that to cut letters
            // in half.
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
                color = clamp((color.r - vAttribs[2]) * vAttribs[3], 0.0, 1.0) * vColor;
            }
        #else
            lowp vec4 color = vColor;
        #endif
        #ifdef TERRAIN
            // depth-writing terrain content: fully transparent fragments (sprite/dash
            // quad corners) must not write depth or they block later style layers
            if (color.a < 0.004) discard;
        #endif
            // Sun and shadow of the ground this lies on, from the TERRAIN normal - the geometry
            // lies flat and has no meaningful normal of its own (see applyTerrainShading).
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
        // Where this fragment sits in the TARGET tile, for the tile clipping in lineFsh.
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
        #ifdef PATTERN
        varying highp_opt vec2 vUV;
        #endif
        varying lowp vec4 vColor;
        varying highp_opt vec2 vDist;
        varying highp_opt float vWidth;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float width = uWidthTable[styleIndex];
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
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
        #ifdef TERRAIN
            // Tangram's line model (extrude in model space, displace onto the terrain) with a
            // CEILING: their quad tapers with distance, which is right, but grows without bound
            // towards the camera and turns a near contour into a blob. Measure the offset on screen
            // and shrink it back to the nominal width when it exceeds it - the factor is <= 1 by
            // construction, so this can never manufacture an oversized quad.
            setTerrainSlopeVaryings(pos);
            vTileUnit = pos.xy * uTileUnitScale + uTileUnitOffset;
            highp vec3 centerPos = applyTerrain(pos);
            applyShadowPos(centerPos);
            highp vec4 centerClip = uMVPMatrix * vec4(centerPos, 1.0);
            highp vec4 edgeClip = uMVPMatrix * vec4(applyTerrain(pos + delta), 1.0);
            highp vec2 edgeDir = edgeClip.xy / edgeClip.w - centerClip.xy / centerClip.w;
            edgeDir = vec2(edgeDir.x * uScreenScale.x, edgeDir.y); // NDC is anisotropic, work in height units
            highp float edgeLen = length(edgeDir);
            // The ceiling is what this vertex is EXTRUDED by, not one line width: a cap corner sits
            // sqrt(2) widths out and an end arrow's barb several, and clamping those to one width
            // squashes the shape they belong to back into the line's own silhouette. The binormal
            // is PACKED (int16, per-geometry scale), so its length only means widths once scaled -
            // raw it is ~32768 and the ceiling never engages at all.
            highp float nominalLen = roundedWidth * length(aVertexBinormal * uBinormalUnitScale) * uScreenScale.y;
            highp vec3 edgePos = applyTerrain(pos + delta);
            if (edgeLen > nominalLen && nominalLen > 0.0) {
                highp float shrink = nominalLen / edgeLen;
                edgeDir = edgeDir * shrink;
                highp vec2 offset = vec2(edgeDir.x / uScreenScale.x, edgeDir.y);
                // The capped vertex keeps the SCREEN offset (so the width is exactly nominal,
                // and neighbouring vertices can not disagree about it) but takes its DEPTH from
                // the terrain-following position it was shrunk towards. Keeping centerClip.z
                // instead put the outer edge of a wide line at the centreline's height, which on
                // a cross-slope is below the ground on the uphill side: the depth test against
                // the terrain surface then ate the line from that side, worst on the widest
                // layer - a route's casing broke up while its fill survived.
                highp vec4 depthClip = uMVPMatrix * vec4(mix(centerPos, edgePos, shrink), 1.0);
                gl_Position = applyDepthBias(vec4((centerClip.xy / centerClip.w + offset) * depthClip.w, depthClip.z, depthClip.w));
            } else {
                gl_Position = applyDepthBias(uMVPMatrix * vec4(edgePos, 1.0));
            }
        #else
            // sample the terrain at the extruded position, so wide lines follow the slope
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
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            // CLIP TO THE TILE. A tile's line buffer is an eighth of a tile wide, so it carries its
            // neighbours' roads and draws them with ITS OWN elevation mapping - the same road twice,
            // at two heights, which coincide looking straight down and separate on a tilt. The
            // stencil masks used to clip this, but there is no stencil buffer on this target.
            // uTileUnitScale is 0 when the tile has no elevation, which disables the test.
            if (uTileUnitScale != vec2(0.0)) {
                if (vTileUnit.x < -0.0005 || vTileUnit.x > 1.0005 || vTileUnit.y < -0.0005 || vTileUnit.y > 1.0005) {
                    discard;
                }
            }
            float dist = vWidth - length(vDist);
            // The antialias ramp is one unit of the quad, and a unit is NOT a pixel: line widths
            // are given in unscaled-DPI units, so on a 2.6x display one unit covers about 1.8
            // device pixels (uAntialiasScale is exactly that ratio, screen height over the
            // normalized resolution). A contour a pixel wide was therefore mostly ramp - the blur.
            // Ramping over one DEVICE pixel keeps a thin line a thin line at any density.
            lowp float a = clamp(dist * uAntialiasScale, 0.0, 1.0);
        #ifdef PATTERN
            lowp vec4 color = texture2D(uPattern, vUV) * vColor * a;
        #else
            lowp vec4 color = vColor * a;
        #endif
        #ifdef TERRAIN
            // depth-writing terrain content: fully transparent fragments (AA aprons,
            // dash gaps) must not write depth or they block later style layers
            if (color.a < 0.004) discard;
        #endif
            // Sun and shadow of the ground this lies on (see pointFsh).
            color = applyTerrainShading(color);
        #ifdef LIGHTING_FSH
            glFragColor = applyFog(applyLighting(color, normalize(vNormal)));
        #else
            glFragColor = applyFog(color);
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
        // 1 where the style slot is a patterned fill, 0 where it is a plain one - so both live in
        // the same geometry and one draw covers them.
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

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            vec3 pos = aVertexPosition;
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            vec4 color = uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = vec3(uUVScale * aVertexUV, uPatternTable[styleIndex]);
        #endif
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            setTerrainSlopeVaryings(pos);
            highp vec3 terrainPos = applyTerrain(pos);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
        }
    )GLSL";

    static const std::string polygonFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        // .xy is the uv, .z the pattern flag - packed so the flag costs no extra varying vector.
        varying highp_opt vec3 vUV;
        #endif
        varying lowp vec4 vColor;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
        #ifdef PATTERN
            // A plain fill sharing the draw has vUV.z 0 and keeps its flat colour.
            lowp vec4 color = mix(vColor, texture2D(uPattern, vUV.xy) * vColor, vUV.z);
        #else
            lowp vec4 color = vColor;
        #endif
        #ifdef TERRAIN
            // depth-writing terrain content: fully transparent fragments (pattern
            // gaps) must not write depth or they block later style layers
            if (color.a < 0.004) discard;
        #endif
            // Sun and shadow of the ground this lies on (see pointFsh).
            color = applyTerrainShading(color);
        #ifdef LIGHTING_FSH
            glFragColor = applyFog(applyLighting(color, normalize(vNormal)));
        #else
            glFragColor = applyFog(color);
        #endif
        }
    )GLSL";

    // The contact shadow an extrusion casts on the ground it stands on: one quad per footprint
    // edge, covering that edge's bounding capsule. It carries no colour of its own - only the
    // fragment's own distance to the segment.
    //
    // Drawn ONLY into the offscreen mask (GLTileRenderer::renderGroundAOMask), under MIN blending,
    // so that a corner, a building:part and a neighbour meeting on one pixel take the darkest of
    // the three. The frame gets the resolved mask multiplied in once, as one screen quad - per-quad
    // compositing would multiply again at every overlap and undo exactly what MIN just resolved.
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
            // (along, across, length) in the segment's own frame, in units of the shadow radius.
            // Affine in the vertex, so interpolating it over the quad is exact.
            vSegment = aVertexBinormal * uBinormalScale;
            // The tile's fade, which reaches here as the style colour's alpha. An extrusion fades
            // in by GROWING, and without this its contact shadow arrives at full strength on a
            // building that is not there yet - a footprint painted on the ground as a tile appears.
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
            // This tile's ground only. Under overzoom one source tile's capsules are handed to
            // every target tile derived from it, and each of those draws displaces them with ITS
            // OWN elevation texture - so without this the same footprint casts a second shadow at
            // a neighbouring tile's height, floating beside the right one.
            if (min(vTilePos.x, vTilePos.y) < -0.01 || max(vTilePos.x, vTilePos.y) > 1.01) {
                discard;
            }
            // Distance to the footprint SEGMENT, per fragment - not a value interpolated between
            // vertices. The caps are what rounds every corner, and what joins one edge's shadow to
            // the next; a per-vertex distance is linear inside a triangle and facets there.
            mediump float t = clamp(vSegment.x, 0.0, vSegment.z);
            // Under the building the ground is fully occluded, so the band holds there instead of
            // falling off (positive across = the side the walls stand on). Alongside the edge only:
            // past its ends that half-plane leaves the footprint, and the next edge's own quad
            // covers what is left. This is also what hides the seam where a draped shadow meets a
            // wall on a slope - the dark side is the side the displacement moves it towards.
            mediump float across = (vSegment.y > 0.0 && vSegment.x == t) ? 0.0 : vSegment.y;
            mediump float dist = min(1.0, length(vec2(vSegment.x - t, across)));
            // Occlusion = (1 - d)^k: full against the wall, zero at the radius, and above 1 it
            // reaches the radius with zero slope so there is no crease to read as an outline.
            // k IS the style's ground-attenuation - the default 1.75 halves the shadow by a third
            // of the way out (0.5 -> 0.3), which is the profile a contact shadow wants.
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
        attribute vec4 aVertexAttribs;
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform mat3 uTileMatrix;
        uniform float uUVScale;
        uniform float uHeightScale;
        uniform vec4 uColorTable[16];
        varying highp_opt vec2 vTilePos;
        varying lowp vec4 vColor;
        #ifdef LIGHTING_FSH
        varying lowp float vWallT;
        varying lowp float vSideVertex;
        varying mediump vec3 vNormal;
        #endif
        #ifdef TERRAIN_SHADOW
        // The extrusion's OWN normal, for the shadow's N.L. Separate from vNormal, which only
        // exists when the lighting runs per fragment - the shadow needs it either way.
        varying mediump vec3 vShadowNormal;
        #endif

        // Ground height at a point of THIS extrusion's footprint frame, transform included.
        float footprintGround(vec2 c) {
            vec3 a = vec3(c, aVertexPosition.z);
        #ifdef TRANSFORM
            a = vec3(uTransformMatrix * vec4(a, 1.0));
        #endif
            return applyTerrain(a).z;
        }

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            // 0 on the roof, 1 on a wall, and BETWEEN on the bevel band that rounds the edge
            // between them (see TileLayerBuilder edge radius). It blends the two normals, which is
            // where the softness comes from - the band itself is one quad, not a subdivided fillet.
            float sideVertex = aVertexAttribs[1] * (1.0 / 127.0);
            // The facade gradient, baked per vertex by the tesselator (appendWallQuad): 0 at the
            // foot of the building, 1 once past the gradient's reach.
            float wallT = aVertexAttribs[3] * (1.0 / 127.0);
            vec3 pos = aVertexPosition;
            // Anything ABOVE the ground is measured from ONE elevation - the footprint's centroid,
            // which the tesselator put in the texcoord slot - so the roof stays level instead of
            // shearing down the slope. The ground ring itself keeps the terrain under each vertex,
            // so the wall still meets the slope everywhere and the walls simply grow taller
            // downhill. mapbox's fill-extrusion base-alignment terrain + height-alignment flat.
            // Anchoring the base to the centroid too (maplibre's rigid prism) buries a building
            // whole wherever the hillside rises more than its own height.
            vec3 anchor = vec3(aVertexUV, aVertexPosition.z);
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
            anchor = vec3(uTransformMatrix * vec4(anchor, 1.0));
        #endif
            float groundZ = applyTerrain(pos).z;
            float baseZ = groundZ;
            if (aVertexHeight > 0.0) {
                // The HIGHEST ground the footprint stands on: the centroid and its reach in four
                // directions, the reach being the tesselator's own footprint extent. The prism is
                // RAISED to it, so the roof stays flat AND no part of the building is left under
                // the hill. Anchoring at the centroid alone buried everything uphill of it;
                // clamping the finished top to the ground instead collapsed those walls to nothing
                // (whole faces missing); taking the ground per vertex bends the roof down the slope.
                float reach = float(aVertexAttribs[2]) * (1.0 / 512.0) / uUVScale;
                baseZ = applyTerrain(anchor).z;
                baseZ = max(baseZ, footprintGround(aVertexUV + vec2(reach, 0.0)));
                baseZ = max(baseZ, footprintGround(aVertexUV - vec2(reach, 0.0)));
                baseZ = max(baseZ, footprintGround(aVertexUV + vec2(0.0, reach)));
                baseZ = max(baseZ, footprintGround(aVertexUV - vec2(0.0, reach)));
            }
            pos = vec3(pos.xy, baseZ) + aVertexNormal * (aVertexHeight * uHeightScale);
            vec3 normal = normalize(mix(aVertexNormal, aVertexBinormal, sideVertex));
            applyShadowPos(pos, normal);
        #ifdef TERRAIN_SHADOW
            vShadowNormal = normal;
        #endif
            vec4 color = uColorTable[styleIndex];
            // The overzoom clip, per VERTEX. Not on the centroid: a building can reach into a tile
            // while its centroid sits in another, and a centroid test then drops it from every tile
            // that holds it - walls missing until a zoom-out retiles them. The position works here
            // because an extrusion's texcoords carry the centroid at the COORD scale (see
            // packGeometry), so uUVScale converts either of them. Y is flipped back out of the
            // transformer's frame - uTileMatrix works in tile space.
            vec2 vertexTile = aVertexPosition.xy * uUVScale;
            vTilePos = (uTileMatrix * vec3(vertexTile.x, 1.0 - vertexTile.y, 1.0)).xy;
        #ifdef LIGHTING_VSH
            // Unshadowed: the shadow is a per-fragment term, so a per-vertex lighting still takes
            // it as a plain multiply in the fragment shader (and loses its ambient doing so).
            vColor = applyLighting3D(color, normal, wallT, sideVertex, 1.0);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = normal;
            vWallT = wallT;
            vSideVertex = sideVertex;
        #endif
            gl_Position = applyDepthBias(uMVPMatrix * vec4(pos, 1.0));
        }
    )GLSL";

    static const std::string polygon3DFsh = R"GLSL(
        varying highp_opt vec2 vTilePos;
        varying lowp vec4 vColor;
        #ifdef TERRAIN_SHADOW
        varying mediump vec3 vShadowNormal;
        #endif
        #ifdef LIGHTING_FSH
        varying lowp float vWallT;
        varying lowp float vSideVertex;
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            if (min(vTilePos.x, vTilePos.y) < -0.01 || max(vTilePos.x, vTilePos.y) > 1.01) {
                discard;
            }
            // Extrusions receive as well as cast: a building in the shadow of a ridge, or of a
            // taller neighbour, darkens the same way the ground does.
            // N.L is the extrusion's own normal, not normal incidence: a roof IS its own caster,
            // so at N.L = 1 it gets the minimum bias against its own depth in the map and speckles
            // itself as soon as a shadow texel covers more ground than a roof is wide - which is
            // what shredded the buildings on zooming out. It also lets the back-face rule shadow
            // a wall facing away from the sun, which no depth comparison can decide.
            mediump float shadow = 1.0;
        #ifdef TERRAIN_SHADOW
            shadow = shadowFactorSlope(max(0.0, dot(normalize(vShadowNormal), uSunDir)));
        #endif
        #ifdef LIGHTING_FSH
            // The shadow goes INTO the lighting, where it dims the sun alone. Multiplied over the
            // finished colour instead it took the ambient with it, and since a wall facing away
            // from the sun is fully shadowed by the back-face rule above, every such wall went
            // black - the whole reason facades did not match mapbox.
            glFragColor = applyFog(applyLighting3D(vColor, normalize(vNormal), vWallT, vSideVertex, shadow));
        #else
            glFragColor = applyFog(vColor);
            glFragColor.rgb *= shadow;
        #endif
        }
    )GLSL";
}

#endif
