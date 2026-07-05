/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_GLTILERENDERERSHADERS_H_
#define _CARTO_VT_GLTILERENDERERSHADERS_H_

namespace carto::vt {
    enum : int {
        A_VERTEXPOSITION,
        A_VERTEXUV,
        A_VERTEXNORMAL,
        A_VERTEXBINORMAL,
        A_VERTEXHEIGHT,
        A_VERTEXCOLOR,
        A_VERTEXATTRIBS
    };

    enum : int {
        U_MVPMATRIX,
        U_TRANSFORMMATRIX,
        U_TILEMATRIX,
        U_UVMATRIX,
        U_BINORMALSCALE,
        U_UVSCALE,
        U_HEIGHTSCALE,
        U_ABSHEIGHTSCALE,
        U_COLORTABLE,
        U_WIDTHTABLE,
        U_OFFSETTABLE,
        U_STROKEWIDTHTABLE,
        U_STROKESCALETABLE,
        U_COLOR,
        U_OPACITY,
        U_PATTERN,
        U_BITMAP,
        U_TEXTURE,
        U_SDFSCALE,
        U_DERIVSCALE,
        U_DEPTHBIAS,
        U_DEPTHBIASCLIP,
        U_ELEVATIONTEXTURE,
        U_ELEVATIONUV,
        U_ELEVATIONDECODE,
        U_ELEVATIONSCALE
    };

    enum : unsigned int {
        TRANSFORM_FLAG   = 1,
        OFFSET_FLAG      = 2,
        PATTERN_FLAG     = 4,
        DERIVATIVES_FLAG = 8,
        TERRAIN_FLAG     = 16,
        TERRAIN_VTF_FLAG = 32
    };

    static const std::map<std::string, int> attribMap = {
        { "aVertexPosition", A_VERTEXPOSITION },
        { "aVertexUV",       A_VERTEXUV },
        { "aVertexNormal",   A_VERTEXNORMAL },
        { "aVertexBinormal", A_VERTEXBINORMAL },
        { "aVertexHeight",   A_VERTEXHEIGHT },
        { "aVertexColor",    A_VERTEXCOLOR },
        { "aVertexAttribs",  A_VERTEXATTRIBS }
    };

    static const std::map<std::string, int> uniformMap = {
        { "uMVPMatrix",        U_MVPMATRIX },
        { "uTransformMatrix",  U_TRANSFORMMATRIX },
        { "uTileMatrix",       U_TILEMATRIX },
        { "uUVMatrix",         U_UVMATRIX },
        { "uBinormalScale",    U_BINORMALSCALE },
        { "uUVScale",          U_UVSCALE },
        { "uHeightScale",      U_HEIGHTSCALE },
        { "uAbsHeightScale",   U_ABSHEIGHTSCALE },
        { "uColorTable",       U_COLORTABLE },
        { "uWidthTable",       U_WIDTHTABLE },
        { "uOffsetTable",      U_OFFSETTABLE },
        { "uStrokeWidthTable", U_STROKEWIDTHTABLE },
        { "uStrokeScaleTable", U_STROKESCALETABLE },
        { "uPattern",          U_PATTERN },
        { "uBitmap",           U_BITMAP },
        { "uTexture",          U_TEXTURE },
        { "uColor",            U_COLOR },
        { "uOpacity",          U_OPACITY },
        { "uSDFScale",         U_SDFSCALE },
        { "uDerivScale",       U_DERIVSCALE },
        { "uDepthBias",        U_DEPTHBIAS },
        { "uDepthBiasClip",    U_DEPTHBIASCLIP },
        { "uElevationTexture", U_ELEVATIONTEXTURE },
        { "uElevationUV",      U_ELEVATIONUV },
        { "uElevationDecode",  U_ELEVATIONDECODE },
        { "uElevationScale",   U_ELEVATIONSCALE }
    };

    static const std::map<unsigned int, std::string> flagDefineMap = {
        { TRANSFORM_FLAG,   "TRANSFORM" },
        { OFFSET_FLAG,      "OFFSET" },
        { PATTERN_FLAG,     "PATTERN" },
        { DERIVATIVES_FLAG, "DERIVATIVES" },
        { TERRAIN_FLAG,     "TERRAIN_DEPTH_BIAS" },
        { TERRAIN_VTF_FLAG, "TERRAIN" }
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
        vec4 applyDepthBias(vec4 clipPos) {
            return vec4(clipPos.xy, clipPos.z - (uDepthBias * clipPos.w + uDepthBiasClip), clipPos.w);
        }
        #else
        vec4 applyDepthBias(vec4 clipPos) {
            return clipPos;
        }
        #endif
        #ifdef TERRAIN
        uniform sampler2D uElevationTexture;
        uniform highp vec4 uElevationUV;     // elevation texture uv = uv.xy + pos.xy * uv.zw
        uniform vec4 uElevationDecode;       // meters = dot(texture sample, decode)
        uniform highp vec4 uElevationScale;  // x: meters to vertex z units (equator), y/z: mercator y = y + pos.y * z, w: vertex frame z offset
        // GPU terrain draping: the vertex z is REPLACED with the height sampled from the
        // elevation texture. Every draped layer samples the same textures, so all layers
        // agree on heights exactly and no geometric depth tolerances are needed.
        // The bilinearly filtered texture sample matches the CPU-side elevation grid
        // sampling (ElevationTileGrid::sampleHeight semantics): samples at texel centers,
        // clamped at edges. The Mercator latitude scale (1/cos) is applied per vertex.
        // The w component maps the absolute height into the vertex frame (tile surface
        // frames are origin-relative and the origin can have a non-zero z).
        vec3 applyTerrain(vec3 pos) {
            float meters = dot(texture2D(uElevationTexture, uElevationUV.xy + pos.xy * uElevationUV.zw), uElevationDecode);
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
    )GLSL";

    static const std::string commonFsh = R"GLSL(
        #ifdef DERIVATIVES
        #extension GL_OES_standard_derivatives : enable
        #endif
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        #define highp_opt highp
        #else
        #define highp_opt mediump
        #endif

        precision mediump float;
    )GLSL";

    static const std::string backgroundVsh = R"GLSL(
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

        void main(void) {
        #ifdef PATTERN
            vUV = aVertexUV * uUVScale;
        #endif
        #ifdef LIGHTING_VSH
            vColor = applyLighting(vec4(1.0, 1.0, 1.0, 1.0), aVertexNormal);
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            gl_Position = applyDepthBias(uMVPMatrix * vec4(applyTerrain(aVertexPosition), 1.0));
        }
    )GLSL";

    static const std::string backgroundFsh = R"GLSL(
        uniform lowp vec4 uColor;
        uniform lowp float uOpacity;
        #ifdef PATTERN
        uniform sampler2D uPattern;
        varying highp_opt vec2 vUV;
        #endif
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif
        #ifdef LIGHTING_VSH
        varying lowp vec4 vColor;
        #endif

        void main(void) {
        #ifdef PATTERN
            lowp vec4 patternColor = texture2D(uPattern, vUV);
            lowp vec4 color = uColor * (1.0 - patternColor.a) + patternColor;
        #else
            lowp vec4 color = uColor;
        #endif
        #if defined(LIGHTING_VSH)
            gl_FragColor = vColor * color * uOpacity;
        #elif defined(LIGHTING_FSH)
            gl_FragColor = applyLighting(color, normalize(vNormal)) * uOpacity;
        #else
            gl_FragColor = color * uOpacity;
        #endif
        }
    )GLSL";

    static const std::string colormapVsh = R"GLSL(
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
        #ifdef LIGHTING_VSH
            vColor = applyLighting(vec4(1.0, 1.0, 1.0, 1.0), aVertexNormal);
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            gl_Position = applyDepthBias(uMVPMatrix * vec4(applyTerrain(aVertexPosition), 1.0));
        }
    )GLSL";

    static const std::string colormapFsh = R"GLSL(
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
        #if defined(LIGHTING_VSH)
            gl_FragColor = vColor * color * uOpacity;
        #elif defined(LIGHTING_FSH)
            gl_FragColor = applyLighting(color, normalize(vNormal)) * uOpacity;
        #else
            gl_FragColor = color * uOpacity;
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
            gl_Position = applyDepthBias(uMVPMatrix * vec4(applyTerrain(aVertexPosition), 1.0));
        }
    )GLSL";

    static const std::string normalmapFsh = R"GLSL(
        uniform sampler2D uBitmap;
        uniform highp_opt vec4 uUVScale;
        uniform lowp float uOpacity;
        varying highp_opt vec2 vUV;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        varying mediump vec3 vBinormal;
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
            mediump vec3 tspaceNormal = packedNormalAlpha.xyz * 2.0 - vec3(1.0, 1.0, 1.0);
            mediump vec3 normal = normalize(vNormal);
            mediump vec3 tangent = normalize(cross(vBinormal, vNormal));
            mediump vec3 binormal = cross(normal, tangent);
            mediump vec3 wspaceNormal = mat3(tangent, binormal, normal) * tspaceNormal;
            mediump float dot = dot(normal, wspaceNormal);
            mediump float intensity = sqrt(max(0.0, 1.0 - dot * dot));
            gl_FragColor = applyLighting(color, wspaceNormal, normal, intensity) * uOpacity;
        #else
            gl_FragColor = color * uOpacity;
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
            gl_FragColor = color * uColor;
        }
    )GLSL";

    static const std::string labelVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        #if defined(LIGHTING_FSH) || defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
        attribute vec2 aVertexUV;
        attribute vec4 aVertexColor;
        attribute vec4 aVertexAttribs;
        uniform mat4 uMVPMatrix;
        uniform vec2 uUVScale;
        uniform float uSDFScale;
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
            vAttribs = vec4(aVertexAttribs[1], uStrokeWidthTable[styleIndex], uSDFScale / size, size / uSDFScale);
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal) * opacity;
        #else
            vColor = color * opacity;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            gl_Position = uMVPMatrix * vec4(aVertexPosition, 1.0);
        }
    )GLSL";

    static const std::string labelFsh = R"GLSL(
        uniform sampler2D uBitmap;
        #ifdef DERIVATIVES
        uniform highp_opt vec2 uDerivScale;
        #endif
        varying lowp vec4 vColor;
        varying highp_opt vec2 vUV;
        varying mediump vec4 vAttribs;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            lowp vec4 color = texture2D(uBitmap, vUV);
            if (vAttribs[0] > 0.0) {
                color = color * vColor;
            } else {
        #ifdef DERIVATIVES
                float size = dot(uDerivScale, fwidth(vUV));
                float scale = 1.0 / size;
        #else
                float size = vAttribs[2];
                float scale = vAttribs[3];
        #endif
                float offset = 0.5 * (1.0 - size - vAttribs[1] * vAttribs[2]);
                color = clamp((color.r - offset) * scale, 0.0, 1.0) * vColor;
            }
        #ifdef LIGHTING_FSH
            gl_FragColor = applyLighting(color, normalize(vNormal));
        #else
            gl_FragColor = color;
        #endif
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
            gl_Position = applyDepthBias(uMVPMatrix * vec4(applyTerrain(pos) + delta, 1.0));
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
        #ifdef LIGHTING_FSH
            gl_FragColor = applyLighting(color, normalize(vNormal));
        #else
            gl_FragColor = color;
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
            // sample the terrain at the extruded position, so wide lines follow the slope
            gl_Position = applyDepthBias(uMVPMatrix * vec4(applyTerrain(pos + delta), 1.0));
        }
    )GLSL";

    static const std::string lineFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        varying highp_opt vec2 vUV;
        #endif
        varying lowp vec4 vColor;
        varying highp_opt vec2 vDist;
        varying highp_opt float vWidth;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            float dist = vWidth - length(vDist);
            lowp float a = clamp(dist, 0.0, 1.0);
        #ifdef PATTERN
            lowp vec4 color = texture2D(uPattern, vUV) * vColor * a;
        #else
            lowp vec4 color = vColor * a;
        #endif
        #ifdef LIGHTING_FSH
            gl_FragColor = applyLighting(color, normalize(vNormal));
        #else
            gl_FragColor = color;
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
        #endif
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform vec4 uColorTable[16];
        #ifdef PATTERN
        varying highp_opt vec2 vUV;
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
            vUV = uUVScale * aVertexUV;
        #endif
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            gl_Position = applyDepthBias(uMVPMatrix * vec4(applyTerrain(pos), 1.0));
        }
    )GLSL";

    static const std::string polygonFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        varying highp_opt vec2 vUV;
        #endif
        varying lowp vec4 vColor;
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
        #ifdef PATTERN
            lowp vec4 color = texture2D(uPattern, vUV) * vColor;
        #else
            lowp vec4 color = vColor;
        #endif
        #ifdef LIGHTING_FSH
            gl_FragColor = applyLighting(color, normalize(vNormal));
        #else
            gl_FragColor = color;
        #endif
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
        uniform float uAbsHeightScale;
        uniform vec4 uColorTable[16];
        varying highp_opt vec2 vTilePos;
        varying lowp vec4 vColor;
        #ifdef LIGHTING_FSH
        varying highp_opt float vHeight;
        varying lowp float vSideVertex;
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float sideVertex = aVertexAttribs[1];
            float height = aVertexHeight * uAbsHeightScale;
            vec3 pos = aVertexPosition;
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            pos = applyTerrain(pos) + aVertexNormal * (aVertexHeight * uHeightScale);
            vec3 normal = normalize(sideVertex > 0.0 ? aVertexBinormal : aVertexNormal);
            vec4 color = uColorTable[styleIndex];
            vTilePos = (uTileMatrix * vec3(aVertexUV * uUVScale, 1.0)).xy;
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, normal, height, sideVertex > 0.0);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = normal;
            vHeight = height;
            vSideVertex = sideVertex;
        #endif
            gl_Position = uMVPMatrix * vec4(pos, 1.0);
        }
    )GLSL";

    static const std::string polygon3DFsh = R"GLSL(
        varying highp_opt vec2 vTilePos;
        varying lowp vec4 vColor;
        #ifdef LIGHTING_FSH
        varying highp_opt float vHeight;
        varying lowp float vSideVertex;
        varying mediump vec3 vNormal;
        #endif

        void main(void) {
            if (min(vTilePos.x, vTilePos.y) < -0.01 || max(vTilePos.x, vTilePos.y) > 1.01) {
                discard;
            }
        #ifdef LIGHTING_FSH
            gl_FragColor = applyLighting(vColor, normalize(vNormal), vHeight, vSideVertex > 0.0);
        #else
            gl_FragColor = vColor;
        #endif
        }
    )GLSL";
}

#endif
