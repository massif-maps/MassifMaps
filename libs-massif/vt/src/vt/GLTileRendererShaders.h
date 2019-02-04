/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_GLTILERENDERERSHADERS_H_
#define _CARTO_VT_GLTILERENDERERSHADERS_H_

namespace carto { namespace vt {
    static const std::string commonVsh = R"GLSL(
        #ifdef LIGHTING
        attribute vec3 aVertexNormal;
        varying mediump vec3 vNormal;

        void calculateNormal() {
            vNormal = aVertexNormal;
        }
        #else
        void calculateNormal() {
        }
        #endif
    )GLSL";

    static const std::string commonFsh = R"GLSL(
        #ifdef DERIVATIVES
        #extension GL_OES_standard_derivatives : enable
        #endif

        precision mediump float;

        #ifdef LIGHTING
        uniform vec3 uLightDir;
        varying mediump vec3 vNormal;
        
        lowp vec4 applyLighting(lowp vec4 color) {
            float lighting = max(0.0, dot(normalize(vNormal), uLightDir)) * 0.5 + 0.5;
            return vec4(color.xyz * lighting, color.w);
        }
        #else
        lowp vec4 applyLighting(lowp vec4 color) {
            return color;
        }
        #endif
    )GLSL";

    static const std::string backgroundVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        #ifdef PATTERN
        attribute vec2 aVertexUV;
        #endif
        uniform mat4 uMVPMatrix;
        #ifdef PATTERN
        uniform vec2 uUVScale;
        varying mediump vec2 vUV;
        #endif

        void main(void) {
        #ifdef PATTERN
            vUV = aVertexUV * uUVScale;
        #endif
            calculateNormal();
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

        void main(void) {
        #ifdef PATTERN
            vec4 patternColor = texture2D(uPattern, vUV);
            gl_FragColor = applyLighting((uColor * (1.0 - patternColor.a) + patternColor) * uOpacity);
        #else
            gl_FragColor = applyLighting(uColor * uOpacity);
        #endif
        }
    )GLSL";

    static const std::string bitmapVsh = R"GLSL(
        attribute vec3 aVertexPosition;
        attribute vec2 aVertexUV;
        uniform mat4 uMVPMatrix;
        uniform mat3 uUVMatrix;
        varying mediump vec2 vUV;

        void main(void) {
            vUV = vec2(uUVMatrix * vec3(aVertexUV, 1.0));
            calculateNormal();
            gl_Position = uMVPMatrix * vec4(aVertexPosition, 1.0);
        }
    )GLSL";

    static const std::string bitmapFsh = R"GLSL(
        uniform sampler2D uBitmap;
        uniform lowp float uOpacity;
        varying mediump vec2 vUV;

        void main(void) {
            gl_FragColor = applyLighting(texture2D(uBitmap, vUV) * uOpacity);
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
        uniform mediump vec2 uInvScreenSize;

        void main(void) {
            vec4 textureColor = texture2D(uTexture, gl_FragCoord.xy * uInvScreenSize);
            gl_FragColor = textureColor * uColor;
        }
    )GLSL";

    static const std::string labelVsh = R"GLSL(
        attribute vec3 aVertexPosition;
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
            vColor = uColorTable[styleIndex] * aVertexAttribs[2] * (1.0 / 127.0);
            vUV = aVertexUV * uUVScale;
            vAttribs = vec4(aVertexAttribs[1], uStrokeWidthTable[styleIndex], uSDFScale / size, size / uSDFScale);
            calculateNormal();
            gl_Position = uMVPMatrix * vec4(aVertexPosition, 1.0);
        }
    )GLSL";

    static const std::string labelFsh = R"GLSL(
        uniform sampler2D uBitmap;
        #ifdef DERIVATIVES
        uniform highp float uDerivScale;
        #endif
        varying lowp vec4 vColor;
        varying mediump vec2 vUV;
        varying mediump vec4 vAttribs;

        void main(void) {
            vec4 color = texture2D(uBitmap, vUV);
            if (vAttribs[0] > 0.5) {
                gl_FragColor = color * vColor.a;
            } else {
                if (vAttribs[0] < -0.5) {
        #ifdef DERIVATIVES
                    float size = dot(vec2(uDerivScale, uDerivScale), fwidth(vUV));
                    float scale = 1.0 / size;
        #else
                    float size = vAttribs[2];
                    float scale = vAttribs[3];
        #endif
                    float offset = 0.5 * (1.0 - size - vAttribs[1] * vAttribs[2]);
                    gl_FragColor = applyLighting(clamp((color.r - offset) * scale, 0.0, 1.0) * vColor);
                } else {
                    gl_FragColor = applyLighting(vec4(0.0, 0.0, 0.0, 0.0));
                }
            }
        }
    )GLSL";

    static const std::string pointVsh = R"GLSL(
        attribute vec3 aVertexPosition;
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
            vColor = uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = uUVScale * aVertexUV;
        #endif
            float offset = 0.5 - 0.5 * uSDFScale / size * (1.0 + uStrokeWidthTable[styleIndex]);
            vAttribs = vec4(aVertexAttribs[1], 0.0, offset, size / uSDFScale);
            calculateNormal();
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
            vec4 color = texture2D(uPattern, vUV);
            if (vAttribs[0] > 0.5) {
                gl_FragColor = applyLighting(color * vColor.a);
            } else {
                gl_FragColor = applyLighting(clamp((color.r - vAttribs[2]) * vAttribs[3], 0.0, 1.0) * vColor);
            }
        #else
            gl_FragColor = applyLighting(vColor);
        #endif
        }
    )GLSL";

    static const std::string lineVsh = R"GLSL(
        attribute vec3 aVertexPosition;
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
        #ifdef PATTERN
        varying mediump vec2 vUV;
        #endif
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        varying highp vec2 vDist;
        varying highp float vWidth;
        #else
        varying mediump vec2 vDist;
        varying mediump float vWidth;
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
            vColor = uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = uUVScale * aVertexUV;
        #endif
            vDist = vec2(aVertexAttribs[1], aVertexAttribs[2]) * (roundedWidth * gamma);
            vWidth = (width - 1.0) * gamma + 1.0;
            calculateNormal();
            gl_Position = uMVPMatrix * vec4(pos + delta, 1.0);
        }
    )GLSL";

    static const std::string lineFsh = R"GLSL(
        #ifdef PATTERN
        uniform sampler2D uPattern;
        #endif
        varying lowp vec4 vColor;
        #ifdef PATTERN
        varying mediump vec2 vUV;
        #endif
        #ifdef GL_FRAGMENT_PRECISION_HIGH
        varying highp vec2 vDist;
        varying highp float vWidth;
        #else
        varying mediump vec2 vDist;
        varying mediump float vWidth;
        #endif

        void main(void) {
            float dist = vWidth - length(vDist);
            lowp float a = clamp(dist, 0.0, 1.0);
        #ifdef PATTERN
            gl_FragColor = applyLighting(texture2D(uPattern, vUV) * vColor * a);
        #else
            gl_FragColor = applyLighting(vColor * a);
        #endif
        }
    )GLSL";

    static const std::string polygonVsh = R"GLSL(
        attribute vec3 aVertexPosition;
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
            vColor = uColorTable[styleIndex];
        #ifdef PATTERN
            vUV = uUVScale * aVertexUV;
        #endif
            calculateNormal();
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
            gl_FragColor = applyLighting(texture2D(uPattern, vUV) * vColor);
        #else
            gl_FragColor = applyLighting(vColor);
        #endif
        }
    )GLSL";

    static const std::string polygon3DVsh = R"GLSL(
        attribute vec3 aVertexPosition;
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
        uniform vec3 uLightDir;
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
            float height = aVertexHeight * uHeightScale;
            vec3 pos = aVertexPosition;
        #ifdef TRANSFORM
            pos = vec3(uTransformMatrix * vec4(pos, 1.0));
        #endif
            pos = pos + aVertexNormal * height;
            vNormal = normalize(aVertexAttribs[1] != 0.0 ? aVertexBinormal : aVertexNormal);
            vTilePos = (uTileMatrix * vec3(aVertexUV * uUVScale, 1.0)).xy;
            vColor = uColorTable[styleIndex];
            vHeight = max(0.0, (aVertexAttribs[1] != 0.0 ? aVertexHeight * uAbsHeightScale : 32.0));
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
            gl_FragColor = applyLighting(vec4(vColor.rgb * (1.0 - 0.75 / (1.0 + vHeight * vHeight)), vColor.a));
        }
    )GLSL";
} }

#endif
