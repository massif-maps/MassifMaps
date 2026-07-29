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
        U_ELEVATIONSCALE,
        U_ELEVATIONTEXELSIZE,
        U_ELEVATIONLATTICECELL,
        U_TERRAINEDGECOARSENING,
        U_LAYERDEPTHOFFSET,
        U_DEPTHSHIFT,
        U_DRAPETEXTURE,
        U_SUNDIR,
        U_SUNCOLOR,
        U_LIGHTPARAMS,
        U_TERRAINSLOPESCALE,
        U_SHADOWMATRIX,
        U_SHADOWTEXTURE,
        U_SHADOWPARAMS,
        U_SHADOWBIAS,
        U_FOGCOLOR,
        U_FOGPARAMS,
        U_DRAPEUVTRANSFORM
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
        FOG_FLAG = 512
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
        { "uElevationScale",   U_ELEVATIONSCALE },
        { "uElevationTexelSize", U_ELEVATIONTEXELSIZE },
        { "uElevationLatticeCell", U_ELEVATIONLATTICECELL },
        { "uTerrainEdgeCoarsening", U_TERRAINEDGECOARSENING },
        { "uLayerDepthOffset",  U_LAYERDEPTHOFFSET },
        { "uDepthShift",        U_DEPTHSHIFT },
        { "uDrapeTexture",      U_DRAPETEXTURE },
        { "uSunDir",            U_SUNDIR },
        { "uSunColor",          U_SUNCOLOR },
        { "uLightParams",       U_LIGHTPARAMS },
        { "uTerrainSlopeScale", U_TERRAINSLOPESCALE },
        { "uShadowMatrix",      U_SHADOWMATRIX },
        { "uShadowTexture",     U_SHADOWTEXTURE },
        { "uShadowParams",      U_SHADOWPARAMS },
        { "uShadowBias",        U_SHADOWBIAS },
        { "uFogColor",          U_FOGCOLOR },
        { "uFogParams",         U_FOGPARAMS },
        { "uDrapeUVTransform",  U_DRAPEUVTRANSFORM }
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
        { FOG_FLAG, "FOG" }
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
        // Two depth models share this function, selected by which uniforms are non-zero:
        //  - slack (occluder) model: pull the draw towards the viewer by
        //    (uDepthBias*w + uDepthBiasClip) so draped content clears the surface pre-pass.
        //  - painter-order model (tangram): separate coincident draped layers by a fixed
        //    per-layer delta, uLayerDepthOffset*(DELTA*w + uDepthShift), DELTA = 2^-19. The
        //    surface is just the bottom layer; there is no occluder and no distance-growing
        //    slack, so far content can not leak in front of a near ridge.
        vec4 applyDepthBias(vec4 clipPos) {
            float z = clipPos.z
                + uLayerDepthOffset * (0.0000019073486328125 * clipPos.w + uDepthShift)
                - (uDepthBias * clipPos.w + uDepthBiasClip);
            return vec4(clipPos.xy, z, clipPos.w);
        }
        #else
        vec4 applyDepthBias(vec4 clipPos) {
            return clipPos;
        }
        #endif
        #ifdef TERRAIN_SHADOW
        // Tile-local -> light clip space, one matrix per cascade. The matrices are built per tile
        // so their input stays in [0,1] and float precision is never asked to hold a world
        // coordinate. EVERY cascade is computed here: which one a fragment ends up using is
        // decided in the fragment stage from the result, and a varying cannot be written
        // conditionally on something only the fragment stage knows.
        uniform highp mat4 uShadowMatrix[4];
        varying highp vec3 vShadowPos0;
        varying highp vec3 vShadowPos1;
        varying highp vec3 vShadowPos2;
        varying highp vec3 vShadowPos3;
        void applyShadowPos(highp vec3 pos) {
            highp vec4 clip0 = uShadowMatrix[0] * vec4(pos, 1.0);
            vShadowPos0 = clip0.xyz / clip0.w * 0.5 + 0.5;
            highp vec4 clip1 = uShadowMatrix[1] * vec4(pos, 1.0);
            vShadowPos1 = clip1.xyz / clip1.w * 0.5 + 0.5;
            highp vec4 clip2 = uShadowMatrix[2] * vec4(pos, 1.0);
            vShadowPos2 = clip2.xyz / clip2.w * 0.5 + 0.5;
            highp vec4 clip3 = uShadowMatrix[3] * vec4(pos, 1.0);
            vShadowPos3 = clip3.xyz / clip3.w * 0.5 + 0.5;
        }
        #else
        void applyShadowPos(highp vec3 pos) {
        }
        #endif
        #ifdef TERRAIN
        uniform sampler2D uElevationTexture;
        uniform highp vec4 uElevationUV;     // elevation texture uv = uv.xy + pos.xy * uv.zw
        uniform vec4 uElevationDecode;       // meters = dot(texture sample, decode)
        uniform highp vec4 uElevationScale;  // x: meters to vertex z units (equator), y/z: mercator y = y + pos.y * z, w: vertex frame z offset
        uniform highp vec4 uElevationTexelSize; // xy: texture size in texels, zw: 1 / size
        uniform highp vec2 uElevationLatticeCell; // regular-grid surface cell size in elevation-uv units (0 = off = sample the full DEM detail)
        uniform highp vec4 uTerrainEdgeCoarsening; // lattice cell scale (2^k, 1 = off) on the west/east/south/north tile edge
        // GPU terrain draping: the vertex z is REPLACED with the height sampled from the
        // elevation texture. Every draped layer samples the same textures, so all layers
        // agree on heights exactly and no geometric depth tolerances are needed.
        // The bilinear filter is applied MANUALLY: 4 samples at exact texel centers +
        // mix. At texel centers NEAREST and LINEAR hardware filtering return the same
        // texel, so the reconstructed height field is identical on every GPU - several
        // mobile GPUs filter VERTEX-stage texture fetches as NEAREST regardless of the
        // requested LINEAR filter, which would make draped geometry deviate from the
        // depth-writing surface meshes by up to a full texel height step (tens of
        // meters on cliffs: content pokes through ridges / needs huge depth slack).
        // The math matches the CPU-side sampling (ElevationTileGrid::sampleHeight
        // semantics): samples at texel centers, clamped at edges (CLAMP_TO_EDGE plus
        // the 1-texel neighbour border in the texture). The Mercator latitude scale
        // (1/cos) is applied per vertex. The w component maps the absolute height into
        // the vertex frame (tile surface frames are origin-relative and the origin can
        // have a non-zero z).
        float sampleElevation(highp vec2 uv) {
            return dot(texture2D(uElevationTexture, uv), uElevationDecode);
        }
        // Full DEM detail: manual bilinear of the elevation texture at uv (4 texel-center taps).
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
        vec3 applyTerrain(vec3 pos) {
            highp vec2 uv = uElevationUV.xy + pos.xy * uElevationUV.zw;
            float meters;
            if (uElevationLatticeCell.x != 0.0) {
                // LATTICE CLAMP (regular-grid surface mode): the reference surface is a
                // regular grid, so its rendered height at any point comes from just the 4
                // surrounding grid vertices. Draped geometry samples the same 4 grid-corner
                // heights (each a full DEM bilinear) and interpolates them with the SAME
                // two-triangle split the surface mesh uses, so it follows the surface exactly
                // (not just at the grid vertices). A bilinear blend instead of the triangle
                // split leaves an in-cell twist that, at low zoom / large cells, exceeds the
                // (near zero, painter-order) depth slack where the surface triangle rises above
                // the bilinear sheet - draped lines then dip behind the surface and crack.
                // The surface mesh (TileSurfaceBuilder::buildRegularGridSurface) emits triangles
                // (a,b,c),(a,c,d) with a=(i,j),b=(i+1,j),c=(i+1,j+1),d=(i,j+1); vertex y is 1-v,
                // which flips the v axis, so in elevation-uv fg-space the corners are
                // d=(0,0)=H00, c=(1,0)=H10, a=(0,1)=H01, b=(1,1)=H11 and the shared edge is the
                // ANTI-diagonal fg.x+fg.y=1 (the H10-H01 split). Match it exactly here.
                // CROSS-LOD STITCHING: on an edge shared with a COARSER neighbouring tile the
                // two tiles otherwise interpolate the DEM between different lattice nodes and
                // the shared edge cracks open (the coarse tile chords across what the fine tile
                // follows). The neighbour's lattice is a strict subset of this tile's lattice
                // (same anchor, cell scaled by 2^k), so scaling the cell along the edge for the
                // vertices ON that edge reproduces the neighbour's chords exactly. The factors
                // are 1 for same-level or finer neighbours, i.e. a no-op by default. Corner
                // vertices sit on a node of every lattice, so a double scaling is harmless.
                highp vec2 cell = uElevationLatticeCell;
                if (pos.x < 0.00001) cell.y *= uTerrainEdgeCoarsening.x;       // west edge
                else if (pos.x > 0.99999) cell.y *= uTerrainEdgeCoarsening.y;  // east edge
                if (pos.y < 0.00001) cell.x *= uTerrainEdgeCoarsening.z;       // south edge
                else if (pos.y > 0.99999) cell.x *= uTerrainEdgeCoarsening.w;  // north edge
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
        #ifdef FOG
        uniform lowp vec4 uFogColor;    // rgb = fog colour, a = how opaque the fog gets at full distance
        uniform highp vec2 uFogParams;  // x = distance where the fog starts, y = 1 / (end - start)

        // Distance from the eye WITHOUT a varying: gl_FragCoord.w is 1/w_clip, and w_clip of a
        // perspective projection is the eye-space depth. An orthographic pass - the drape bake -
        // has w = 1, which is a whole world in internal units, so it never fogs: exactly right,
        // since the bake is flat content that gets fogged later as part of the terrain surface.
        lowp vec4 applyFog(lowp vec4 color) {
            highp float dist = 1.0 / max(1.0e-9, gl_FragCoord.w);
            lowp float amount = clamp((dist - uFogParams.x) * uFogParams.y, 0.0, 1.0) * uFogColor.a;
            // Colours here are PREMULTIPLIED, so the fog colour has to be premultiplied by this
            // fragment's own alpha; and the fog tints what is there rather than adding coverage,
            // so alpha is left alone.
            return vec4(mix(color.rgb, uFogColor.rgb * color.a, amount), color.a);
        }
        #else
        lowp vec4 applyFog(lowp vec4 color) {
            return color;
        }
        #endif
        #ifdef TERRAIN_SHADOW
        uniform sampler2D uShadowTexture;
        uniform mediump vec4 uShadowParams; // x = 1/mapSize within one cascade, y = strength, z = PCF radius in texels, w = 1/cascade count
        uniform mediump vec4 uShadowBias;   // normalised depth bias, per cascade
        varying highp vec3 vShadowPos0;
        varying highp vec3 vShadowPos1;
        varying highp vec3 vShadowPos2;
        varying highp vec3 vShadowPos3;

        // True when a light-space position does not land inside its cascade's page, with room for
        // the PCF kernel: such a fragment has to fall back to the next, coarser cascade.
        bool outsideShadowPage(highp vec3 pos, mediump float margin) {
            return pos.x < margin || pos.x > 1.0 - margin || pos.y < margin || pos.y > 1.0 - margin || pos.z < 0.0 || pos.z > 1.0;
        }

        // The caster pass packs window-space depth into RGB; unpack and compare with a slope
        // independent constant plus the caller's bias. The uv is in ATLAS space: the cascades are
        // pages of one texture, side by side, near page first.
        highp float shadowDepth(highp vec2 uv) {
            highp vec4 enc = texture2D(uShadowTexture, uv);
            return dot(enc.rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
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
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos1;
                page = 1.0;
                bias = uShadowBias.y;
            }
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos2;
                page = 2.0;
                bias = uShadowBias.z;
            }
            if (outsideShadowPage(pos, margin)) {
                pos = vShadowPos3;
                page = 3.0;
                bias = uShadowBias.w;
            }
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
            if (pos.x < 0.0 || pos.x > 1.0 || pos.y < 0.0 || pos.y > 1.0 || pos.z < 0.0 || pos.z > 1.0) {
                // Outside every cascade: unshadowed rather than black - but the back-face rule
                // below still applies, or the ground would brighten along a hard ring exactly where
                // the last cascade ends.
                return mix(1.0, smoothstep(0.0, 0.15, ndl), uShadowParams.y);
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
            for (int j = -1; j <= 1; j++) {
                for (int i = -1; i <= 1; i++) {
                    highp vec2 offset = vec2(float(i) * o, float(j) * o);
                    lit += ref + dot(offset, dzduv) <= shadowDepth(atlasBase + (pos.xy + offset) * atlasScale) ? 1.0 : 0.0;
                }
            }
            lit *= 1.0 / 9.0;
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
            lit = min(lit, smoothstep(0.0, 0.15, ndl));
            return mix(1.0, lit, uShadowParams.y);
        }
        mediump float shadowFactor() {
            return shadowFactorSlope(1.0);
        }
        #endif
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
        #ifdef DRAPE
        // xy = uv offset, zw = uv scale. Identity for a tile drawn with its own drape texture;
        // a sub-rect when the tile is standing in on an ancestor's texture because its own is
        // not baked yet.
        uniform highp vec4 uDrapeUVTransform;
        varying highp_opt vec2 vDrapeUV;
        #endif
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
        // Per-fragment terrain lighting needs the elevation uv of the fragment and the local
        // mercator height stretch; both are linear in the vertex position, so they interpolate.
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;
        #endif

        void main(void) {
        #ifdef PATTERN
            vUV = aVertexUV * uUVScale;
        #endif
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
            vElevUV = uElevationUV.xy + aVertexPosition.xy * uElevationUV.zw;
            highp float lightMY = uElevationScale.y + aVertexPosition.y * uElevationScale.z;
            vElevCosh = 0.5 * (exp(lightMY) + exp(-lightMY));
        #endif
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
            gl_FragColor = vec4(enc, 1.0);
        }
    )GLSL";

    static const std::string backgroundFsh = R"GLSL(
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
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
        // Precision qualifiers must match the vertex-stage declarations exactly, or the
        // program fails to LINK (same name, different precision is an error in GLSL ES 1.00).
        uniform sampler2D uElevationTexture;
        uniform highp vec4 uElevationDecode; // 'vec4' in the vertex stage means highp there
        uniform highp vec4 uElevationTexelSize;
        uniform mediump vec3 uSunDir;         // east, north, up - the same frame the tile mesh lives in
        uniform lowp vec4 uSunColor;          // rgb = colour, a = unused
        uniform mediump vec2 uLightParams;    // x = sun intensity, y = ambient intensity
        uniform highp vec2 uTerrainSlopeScale; // metres of height -> world units, per elevation-uv unit
        varying highp vec2 vElevUV;
        varying mediump float vElevCosh;

        // Central difference on the DEM, one texel each way. The surface is displaced by exactly
        // this height field in the vertex stage, so the normal is the normal of what is drawn -
        // no second DEM decode, no pre-baked normal map, and it follows the live sun.
        mediump vec3 terrainNormal() {
            // Heights are metres and reach several thousand: mediump would quantise them to
            // whole metres and the central difference would be mostly rounding noise.
            highp vec2 st = uElevationTexelSize.zw;
            highp float hL = dot(texture2D(uElevationTexture, vElevUV - vec2(st.x, 0.0)), uElevationDecode);
            highp float hR = dot(texture2D(uElevationTexture, vElevUV + vec2(st.x, 0.0)), uElevationDecode);
            highp float hD = dot(texture2D(uElevationTexture, vElevUV - vec2(0.0, st.y)), uElevationDecode);
            highp float hU = dot(texture2D(uElevationTexture, vElevUV + vec2(0.0, st.y)), uElevationDecode);
            highp float dx = (hR - hL) * uTerrainSlopeScale.x * vElevCosh / (2.0 * st.x);
            highp float dy = (hU - hD) * uTerrainSlopeScale.y * vElevCosh / (2.0 * st.y);
            return normalize(vec3(-dx, -dy, 1.0));
        }
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
            // Normalised Lambert: ambient is the floor, the sun fills the REMAINING headroom, so
            // a surface facing the sun lands at 1 instead of ambient+1. Adding them blows the
            // ground out to white at a high sun, and a clipped highlight cannot show a shadow.
            mediump vec3 lit = vec3(uLightParams.y) + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
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
            gl_FragColor = applyFog(vColor * color * uOpacity);
        #elif defined(LIGHTING_FSH)
            gl_FragColor = applyFog(applyLighting(color, normalize(vNormal)) * uOpacity);
        #else
            gl_FragColor = applyFog(color * uOpacity);
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
            highp vec3 terrainPos = applyTerrain(aVertexPosition);
            applyShadowPos(terrainPos);
            gl_Position = applyDepthBias(uMVPMatrix * vec4(terrainPos, 1.0));
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
        #if defined(TERRAIN_LIGHT) && defined(TERRAIN)
            // The draped colour is premultiplied, so scaling rgb alone is a valid tint; clamp
            // back to alpha so an intensity above 1 cannot break premultiplication.
            mediump float ndl = max(0.0, dot(terrainNormal(), uSunDir));
            // Normalised Lambert: ambient is the floor, the sun fills the REMAINING headroom, so
            // a surface facing the sun lands at 1 instead of ambient+1. Adding them blows the
            // ground out to white at a high sun, and a clipped highlight cannot show a shadow.
            mediump vec3 lit = vec3(uLightParams.y) + uSunColor.rgb * ((1.0 - uLightParams.y) * ndl * uLightParams.x);
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
            gl_FragColor = applyFog(vColor * color * uOpacity);
        #elif defined(LIGHTING_FSH)
            gl_FragColor = applyFog(applyLighting(color, normalize(vNormal)) * uOpacity);
        #else
            gl_FragColor = applyFog(color * uOpacity);
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
            gl_FragColor = applyFog(shade * uOpacity);
        #else
            gl_FragColor = applyFog(color * uOpacity);
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
        #ifdef TERRAIN
            // depth-writing terrain content: fully transparent fragments (sprite/dash
            // quad corners) must not write depth or they block later style layers
            if (color.a < 0.004) discard;
        #endif
        #ifdef LIGHTING_FSH
            gl_FragColor = applyFog(applyLighting(color, normalize(vNormal)));
        #else
            gl_FragColor = applyFog(color);
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
        #ifdef TERRAIN
            // depth-writing terrain content: fully transparent fragments (AA aprons,
            // dash gaps) must not write depth or they block later style layers
            if (color.a < 0.004) discard;
        #endif
        #ifdef LIGHTING_FSH
            gl_FragColor = applyFog(applyLighting(color, normalize(vNormal)));
        #else
            gl_FragColor = applyFog(color);
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
        #ifdef TERRAIN
            // depth-writing terrain content: fully transparent fragments (pattern
            // gaps) must not write depth or they block later style layers
            if (color.a < 0.004) discard;
        #endif
        #ifdef LIGHTING_FSH
            gl_FragColor = applyFog(applyLighting(color, normalize(vNormal)));
        #else
            gl_FragColor = applyFog(color);
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
            applyShadowPos(pos);
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
            gl_Position = applyDepthBias(uMVPMatrix * vec4(pos, 1.0));
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
            gl_FragColor = applyFog(applyLighting(vColor, normalize(vNormal), vHeight, vSideVertex > 0.0));
        #else
            gl_FragColor = applyFog(vColor);
        #endif
        #ifdef TERRAIN_SHADOW
            // Extrusions receive as well as cast: a building in the shadow of a ridge, or of a
            // taller neighbour, darkens the same way the ground does.
            gl_FragColor.rgb *= shadowFactor();
        #endif
        }
    )GLSL";
}

#endif
