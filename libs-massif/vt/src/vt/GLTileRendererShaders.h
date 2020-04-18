/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_GLTILERENDERERSHADERS_H_
#define _CARTO_VT_GLTILERENDERERSHADERS_H_

namespace carto { namespace vt {
    enum {
        A_VERTEXPOSITION,
        A_VERTEXUV,
        A_VERTEXNORMAL,
        A_VERTEXBINORMAL,
        A_VERTEXHEIGHT,
        A_VERTEXCOLOR,
        A_VERTEXATTRIBS
    };

    enum {
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
        U_STROKEWIDTHTABLE,
        U_COLOR,
        U_OPACITY,
        U_PATTERN,
        U_BITMAP,
        U_TEXTURE,
        U_SDFSCALE,
        U_HALFRESOLUTION,
        U_INVSCREENSIZE,
        U_DERIVSCALE,
        U_GAMMA
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
        { "uStrokeWidthTable", U_STROKEWIDTHTABLE },
        { "uPattern",          U_PATTERN },
        { "uBitmap",           U_BITMAP },
        { "uTexture",          U_TEXTURE },
        { "uColor",            U_COLOR },
        { "uOpacity",          U_OPACITY },
        { "uSDFScale",         U_SDFSCALE },
        { "uHalfResolution",   U_HALFRESOLUTION },
        { "uInvScreenSize",    U_INVSCREENSIZE },
        { "uDerivScale",       U_DERIVSCALE },
        { "uGamma",            U_GAMMA }
    };

    static const std::string commonVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        #ifdef LIGHTING_FSH
        attribute vec3 aVertexNormal;
        varying mediump vec3 vNormal;
        #elif defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
    )GLSL";

    static const std::string commonFsh = R"GLSL(
        #ifdef DERIVATIVES
        #extension GL_OES_standard_derivatives : enable
        #endif
        #ifdef LIGHTING_FSH
        varying mediump vec3 vNormal;
        #endif

        precision mediump float;
    )GLSL";

    static const std::string backgroundVsh = R"GLSL(
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        #endif
        uniform mat4 uMVPMatrix;
        #ifdef PATTERN
        uniform vec2 uUVScale;
        varying mediump vec2 vUV;
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
            gl_Position = uMVPMatrix * vec4(aVertexPosition, 1.0);
        }
    )GLSL";

    static const std::string backgroundFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        #endif
        uniform lowp vec4 uColor;
        uniform lowp float uOpacity;
        #ifdef PATTERN
        varying mediump vec2 vUV;
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
        #ifdef LIGHTING_VSH
            gl_FragColor = vColor * color * uOpacity;
        #elif defined(LIGHTING_FSH)
            gl_FragColor = applyLighting(color, normalize(vNormal)) * uOpacity;
        #else
            gl_FragColor = color * uOpacity;
        #endif
        }
    )GLSL";

    static const std::string bitmapVsh = R"GLSL(
        attribute vec2 aVertexUV;
        uniform mat4 uMVPMatrix;
        uniform mat3 uUVMatrix;
        varying mediump vec2 vUV;
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
            gl_Position = uMVPMatrix * vec4(aVertexPosition, 1.0);
        }
    )GLSL";

    static const std::string bitmapFsh = R"GLSL(
        uniform sampler2D uBitmap;
        uniform lowp float uOpacity;
        varying mediump vec2 vUV;
        #ifdef LIGHTING_VSH
        varying lowp vec4 vColor;
        #endif

        void main(void) {
            lowp vec4 color = texture2D(uBitmap, vUV);
        #ifdef LIGHTING_VSH
            gl_FragColor = vColor * color * uOpacity;
        #elif defined(LIGHTING_FSH)
            gl_FragColor = applyLighting(color, normalize(vNormal)) * uOpacity;
        #else
            gl_FragColor = color * uOpacity;
        #endif
        }
    )GLSL";

    static const std::string blendVsh = R"GLSL(
        uniform mat4 uMVPMatrix;

        void main(void) {
            gl_Position = uMVPMatrix * vec4(aVertexPosition, 1.0);
        }
    )GLSL";

    static const std::string blendFsh = R"GLSL(
        uniform sampler2D uTexture;
        uniform lowp vec4 uColor;
        uniform mediump vec2 uInvScreenSize;

        void main(void) {
            lowp vec4 color = texture2D(uTexture, gl_FragCoord.xy * uInvScreenSize);
            gl_FragColor = color * uColor;
        }
    )GLSL";

    static const std::string labelVsh = R"GLSL(
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
        varying mediump vec2 vUV;
        varying mediump vec4 vAttribs;

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float size = uWidthTable[styleIndex];
            vec4 color = uColorTable[styleIndex] * aVertexAttribs[2] * (1.0 / 127.0);
            vUV = aVertexUV * uUVScale;
            vAttribs = vec4(aVertexAttribs[1], uStrokeWidthTable[styleIndex], uSDFScale / size, size / uSDFScale);
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal);
        #else
            vColor = color;
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
        uniform highp vec2 uDerivScale;
        #endif
        varying lowp vec4 vColor;
        varying mediump vec2 vUV;
        varying mediump vec4 vAttribs;

        void main(void) {
            lowp vec4 color = texture2D(uBitmap, vUV);
            if (vAttribs[0] > 0.5) {
                color = color * vColor.a;
            } else {
                if (vAttribs[0] < -0.5) {
        #ifdef DERIVATIVES
                    float size = dot(uDerivScale, fwidth(vUV));
                    float scale = 1.0 / size;
        #else
                    float size = vAttribs[2];
                    float scale = vAttribs[3];
        #endif
                    float offset = 0.5 * (1.0 - size - vAttribs[1] * vAttribs[2]);
                    color = clamp((color.r - offset) * scale, 0.0, 1.0) * vColor;
                } else {
                    color = vec4(0.0, 0.0, 0.0, 0.0);
                }
            }
        #ifdef LIGHTING_FSH
            gl_FragColor = applyLighting(color, normalize(vNormal));
        #else
            gl_FragColor = color;
        #endif
        }
    )GLSL";

    static const std::string pointVsh = R"GLSL(
        attribute vec3 aVertexBinormal;
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        #endif
        attribute vec4 aVertexAttribs;
        #ifdef PATTERN
        uniform vec2 uUVScale;
        #endif
        uniform float uBinormalScale;
        uniform float uSDFScale;
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform vec4 uColorTable[16];
        uniform float uWidthTable[16];
        uniform float uStrokeWidthTable[16];
        varying lowp vec4 vColor;
        #ifdef PATTERN
        varying mediump vec2 vUV;
        #endif
        varying mediump vec4 vAttribs;

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float size = uWidthTable[styleIndex];
            vec3 pos = aVertexPosition;
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            vec3 delta = aVertexBinormal * (uBinormalScale * size);
            vec4 color = uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = uUVScale * aVertexUV;
        #endif
            float offset = 0.5 - 0.5 * uSDFScale / size * (1.0 + uStrokeWidthTable[styleIndex]);
            vAttribs = vec4(aVertexAttribs[1], 0.0, offset, size / uSDFScale);
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            gl_Position = uMVPMatrix * vec4(pos + delta, 1.0);
        }
    )GLSL";

    static const std::string pointFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        #endif
        varying lowp vec4 vColor;
        #ifdef PATTERN
        varying mediump vec2 vUV;
        #endif
        varying mediump vec4 vAttribs;

        void main(void) {
        #ifdef PATTERN
            lowp vec4 color = texture2D(uPattern, vUV);
            if (vAttribs[0] > 0.5) {
                color = color * vColor.a;
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
        attribute vec3 aVertexBinormal;
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        #endif
        attribute vec4 aVertexAttribs;
        #ifdef PATTERN
        uniform vec2 uUVScale;
        #endif
        uniform float uBinormalScale;
        uniform float uHalfResolution;
        uniform float uGamma;
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform vec4 uColorTable[16];
        uniform float uWidthTable[16];
        varying lowp vec4 vColor;
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        varying highp vec2 vDist;
        varying highp float vWidth;
        #ifdef PATTERN
        varying highp vec2 vUV;
        #endif
        #else
        varying mediump vec2 vDist;
        varying mediump float vWidth;
        #ifdef PATTERN
        varying mediump vec2 vUV;
        #endif
        #endif

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            float width = uWidthTable[styleIndex] * uHalfResolution;
            float roundedWidth = width + float(width > 0.0);
            float gamma = uGamma * aVertexAttribs[3];
            vec3 pos = aVertexPosition;
            vec3 delta = aVertexBinormal * (uBinormalScale * roundedWidth);
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            vec4 color = uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = uUVScale * aVertexUV;
        #endif
            vDist = vec2(aVertexAttribs[1], aVertexAttribs[2]) * (roundedWidth * gamma);
            vWidth = (width - 1.0) * gamma + 1.0;
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, aVertexNormal);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = aVertexNormal;
        #endif
            gl_Position = uMVPMatrix * vec4(pos + delta, 1.0);
        }
    )GLSL";

    static const std::string lineFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        #endif
        varying lowp vec4 vColor;
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        varying highp vec2 vDist;
        varying highp float vWidth;
        #ifdef PATTERN
        varying highp vec2 vUV;
        #endif
        #else
        varying mediump vec2 vDist;
        varying mediump float vWidth;
        #ifdef PATTERN
        varying mediump vec2 vUV;
        #endif
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
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        #endif
        attribute vec4 aVertexAttribs;
        #ifdef PATTERN
        uniform vec2 uUVScale;
        #endif
        #ifdef TRANSFORM
        uniform mat4 uTransformMatrix;
        #endif
        uniform mat4 uMVPMatrix;
        uniform vec4 uColorTable[16];
        varying lowp vec4 vColor;
        #ifdef PATTERN
        varying mediump vec2 vUV;
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
            gl_Position = uMVPMatrix * vec4(pos, 1.0);
        }
    )GLSL";

    static const std::string polygonFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        #endif
        varying lowp vec4 vColor;
        #ifdef PATTERN
        varying mediump vec2 vUV;
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
        #if !defined(LIGHTING_FSH) && !defined(LIGHTING_VSH)
        attribute vec3 aVertexNormal;
        #endif
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
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        varying highp vec2 vTilePos;
        varying highp float vHeight;
        #else
        varying mediump vec2 vTilePos;
        varying mediump float vHeight;
        #endif
        varying lowp vec4 vColor;

        void main(void) {
            int styleIndex = int(aVertexAttribs[0]);
            bool sideVertex = aVertexAttribs[1] != 0.0;
            float height = aVertexHeight * uHeightScale;
            vec3 pos = aVertexPosition;
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            pos = pos + aVertexNormal * height;
            vec3 normal = normalize(sideVertex ? aVertexBinormal : aVertexNormal);
            vTilePos = (uTileMatrix * vec3(aVertexUV * uUVScale, 1.0)).xy;
            vec4 color = uColorTable[styleIndex];
        #ifdef LIGHTING_VSH
            vColor = applyLighting(color, normal);
        #else
            vColor = color;
        #endif
        #ifdef LIGHTING_FSH
            vNormal = normal;
        #endif
            vHeight = max(0.0, (sideVertex ? aVertexHeight * uAbsHeightScale : 32.0));
            gl_Position = uMVPMatrix * vec4(pos, 1.0);
        }
    )GLSL";

    static const std::string polygon3DFsh = R"GLSL(
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        varying highp vec2 vTilePos;
        varying highp float vHeight;
        #else
        varying mediump vec2 vTilePos;
        varying mediump float vHeight;
        #endif
        varying lowp vec4 vColor;

        void main(void) {
            if (min(vTilePos.x, vTilePos.y) < -0.01 || max(vTilePos.x, vTilePos.y) > 1.01) {
                discard;
            }
            lowp vec4 color = vec4(vColor.rgb * (1.0 - 0.75 / (1.0 + vHeight * vHeight)), vColor.a);
        #ifdef LIGHTING_FSH
            gl_FragColor = applyLighting(color, normalize(vNormal));
        #else
            gl_FragColor = color;
        #endif
        }
    )GLSL";
} }

#endif
