#version 100
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform sampler2D uColorTex;
uniform sampler2D uTerrainDepthTex;
uniform vec2 uInvScreenSize;
uniform float uFar;
uniform float uIntensity;
// 0 geo-three's (linear depth over uDepthNear..uDepthFar), 1 ours relative to the depth, 2 silhouettes
// only (peakfinder.com's lines) - see operatorAt.
uniform float uOperator;
uniform float uOutlineWidth;
uniform float uOutlineGain;
uniform float uOutlinePower;
uniform float uOutlineFloor;
uniform float uOutlineCeiling;
uniform float uInkSky;
uniform float uHorizonBoost;
uniform float uHorizonWidth;
// The camera range the outline measures depth over, metres. uDepthFar 0 is the frame's own far plane.
uniform float uDepthNear;
uniform float uDepthFar;
// The reference's hardware depth: its bits (0 keeps our own linear depth) and world units per metre.
uniform float uDepthBits;
uniform float uDepthUnit;
uniform float uMetersPerUnit;
const vec4 uInkColor = vec4(0, 0, 0, 1);
// AR: the frame is a hole for the camera preview and only the ink is drawn - see reliefOutlineShader.
uniform float uTransparent;

// Lens distortion, for AR. k1..k3 radial and p1, p2 tangential, exactly as Camera2's LENS_DISTORTION
// states them; uDistortCenter* is the principal point's offset in the same tangent units. Every
// coefficient zero - the default, and every non-AR frame - makes distortUv the identity.
// Scalars and not vectors because PostProcessEffect carries float and colour uniforms only.
uniform float uDistortK1;
uniform float uDistortK2;
uniform float uDistortK3;
uniform float uDistortP1;
uniform float uDistortP2;
uniform float uDistortCenterX;
uniform float uDistortCenterY;
// Half-field TANGENTS: what the camera frame spans on SCREEN, and what this frame was RENDERED to
// span. The render is the wider of the two, by exactly enough that undistorting the screen's corners
// still lands inside it - see arGeometry in features/peakFinder.ts. Equal when there is no warp.
uniform float uDistortScreenTanX;
uniform float uDistortScreenTanY;
uniform float uDistortRenderTanX;
uniform float uDistortRenderTanY;
// 1 when the view is a quarter turn from the camera's own landscape frame, which is what a portrait
// AR view is. The COORDINATES are rotated rather than the coefficients: that way k1..k3 and p1, p2
// are used exactly as the platform states them, and only this one mapping carries the orientation.
uniform float uDistortRotate;

// Where to sample the RECTILINEAR render for the pixel a distorted camera would put here.
//
// The camera's picture is distorted and the render is not, so for an output pixel the render has to
// be read at the IDEAL position that the lens maps onto it - the inverse of Brown-Conrady. There is
// no closed form, so it is the standard fixed-point iteration: divide out the radial term at the
// current estimate and repeat. Phone-scale distortion converges in two or three rounds.
//
// The render covers a wider field than the screen, which is what makes this possible at all: barrel
// distortion pulls the periphery inwards, so the ideal position for a screen CORNER lies outside the
// screen's own field, and a render that stopped at the screen's field would have nothing there to
// read but its own edge.
vec2 distortUv(vec2 uv) {
    if (abs(uDistortK1) + abs(uDistortK2) + abs(uDistortK3) + abs(uDistortP1) + abs(uDistortP2) == 0.0) {
        return uv;
    }
    vec2 center = vec2(uDistortCenterX, uDistortCenterY);
    // Screen NDC into the tangent space the coefficients are stated in, about the principal point.
    vec2 viewTan = (uv * 2.0 - 1.0) * vec2(uDistortScreenTanX, uDistortScreenTanY);
    vec2 distorted = (uDistortRotate > 0.5 ? vec2(viewTan.y, -viewTan.x) : viewTan) - center;
    vec2 ideal = distorted;
    for (int i = 0; i < 3; i++) {
        float r2 = dot(ideal, ideal);
        float radial = 1.0 + r2 * (uDistortK1 + r2 * (uDistortK2 + r2 * uDistortK3));
        vec2 tangential = vec2(2.0 * uDistortP1 * ideal.x * ideal.y + uDistortP2 * (r2 + 2.0 * ideal.x * ideal.x),
                               uDistortP1 * (r2 + 2.0 * ideal.y * ideal.y) + 2.0 * uDistortP2 * ideal.x * ideal.y);
        ideal = (distorted - tangential) / max(radial, 0.1);
    }
    // ...back into the view's frame, and out through the RENDER's field, which is the wider one.
    vec2 idealView = ideal + center;
    idealView = uDistortRotate > 0.5 ? vec2(-idealView.y, idealView.x) : idealView;
    return (idealView / vec2(uDistortRenderTanX, uDistortRenderTanY)) * 0.5 + 0.5;
}


float unpackDepth(vec4 c) {
    return dot(c.rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
}
float coverage(vec4 c) {
    return c.a;
}
float depthAt(vec2 uv) {
    return unpackDepth(texture2D(uTerrainDepthTex, uv));
}
float metresAt(vec4 c) {
    return unpackDepth(c) * uFar * uMetersPerUnit;
}
/**
 * The reference's depth, which is not ours.
 *
 * geo-three's operator reads viewZToOrthographicDepth(getViewZ(depth), cameraNear, cameraFar) -
 * a LINEAR depth over the camera's own near..far, which its settings put at 10 m and 173 km. Ours
 * is a fraction of a far plane that a panorama recomputes every frame and that reaches 1229 km, so
 * the same ground step lands ~7x smaller in it. No parameter value transfers between the two: the
 * reference's depthMultiplier of 11 means something else applied to our numbers, which is most of
 * why matching its render by tuning never converged.
 *
 * The sky is 1, as its cleared depth buffer is. Ground past uDepthFar is NOT clipped to it: the
 * reference never draws any, but a view reaching further should still ink its ranges.
 */
float linearDepthAt(vec2 uv) {
    vec4 c = texture2D(uTerrainDepthTex, uv);
    if (coverage(c) < 0.5) {
        return 1.0;
    }
    float metres = metresAt(c);
    float farMetres = uDepthFar > 0.0 ? uDepthFar : uFar * uMetersPerUnit;
    if (uDepthBits > 0.0) {
        // The reference's depth: a perspective depth over near..far in its own world units
        // (uDepthUnit per metre), stored in uDepthBits bits and linearised back by three.js in
        // float. Its step grows with the square of the distance, so far slopes band rather than
        // shade. Same expressions as perspectiveDepthToViewZ and viewZToOrthographicDepth.
        float near = uDepthNear * uDepthUnit;
        float far = farMetres * uDepthUnit;
        float hardware = (far / (far - near)) * (1.0 - near / max(metres * uDepthUnit, near));
        float steps = exp2(uDepthBits) - 1.0;
        hardware = floor(hardware * steps + 0.5) / steps;
        float viewZ = (near * far) / ((far - near) * hardware - far);
        return (viewZ + near) / (near - far);
    }
    return max((metres - uDepthNear) / max(farMetres - uDepthNear, 1.0), 0.0);
}
// 1 / distance, 0 for the sky: on any PLANE this is linear in screen space, so its laplacian is zero
// on every slope however steep or far, and only a crease or an occlusion survives it.
float inverseDepthAt(vec2 uv) {
    vec4 c = texture2D(uTerrainDepthTex, uv);
    return coverage(c) < 0.5 ? 0.0 : 1.0 / max(metresAt(c), 1.0);
}

// The outline operator at one pixel, over taps one pixel away.
float operatorAt(vec2 uv) {
    vec2 offset = uInvScreenSize;
    if (uOperator > 1.5) {
        // SILHOUETTES ONLY, which is what peakfinder.com's black lines are (a line pass of their own,
        // not their shading). The laplacian of inverse depth, NEAR side only: negative where the
        // neighbours are further than the plane through this pixel would put them, i.e. where this
        // pixel hides ground behind it. Relative, so a jump of a given fraction inks the same near or
        // far, and a sky neighbour counts as infinitely far - the skyline is a silhouette too.
        float centre = inverseDepthAt(uv);
        if (centre <= 0.0) {
            return 0.0;
        }
        float laplacian = inverseDepthAt(uv + vec2(offset.x, 0.0)) + inverseDepthAt(uv - vec2(offset.x, 0.0))
                        + inverseDepthAt(uv + vec2(0.0, offset.y)) + inverseDepthAt(uv - vec2(0.0, offset.y))
                        - 4.0 * centre;
        return max(-laplacian / centre, 0.0);
    }
    if (uOperator > 0.5) {
        float depth = depthAt(uv);
        float diff = abs(depth - depthAt(uv + vec2(offset.x, 0.0)))
                   + abs(depth - depthAt(uv - vec2(offset.x, 0.0)))
                   + abs(depth - depthAt(uv + vec2(0.0, offset.y)))
                   + abs(depth - depthAt(uv - vec2(0.0, offset.y)));
        // Scaled by the depth itself, so a far ridge inks like a near one: the same ground step is a
        // smaller fraction of the far plane the further away it is.
        return diff / max(depth, 0.0001);
    }
    // The reference's operator, term for term: four taps of a LINEAR depth, summed as absolute
    // differences, and no division by anything. The scale lives in uDepthNear/uDepthFar.
    float centreLinear = linearDepthAt(uv);
    return abs(centreLinear - linearDepthAt(uv + vec2(offset.x, 0.0)))
         + abs(centreLinear - linearDepthAt(uv - vec2(offset.x, 0.0)))
         + abs(centreLinear - linearDepthAt(uv + vec2(0.0, offset.y)))
         + abs(centreLinear - linearDepthAt(uv - vec2(0.0, offset.y)));
}

void main(void) {
    // The post-process vertex stage passes no varying, so the uv is the fragment's own coordinate -
    // through the lens warp, so the scene, the depth and every tap stay registered in AR.
    vec2 v_uv = distortUv(gl_FragCoord.xy * uInvScreenSize);
    vec4 color = texture2D(uColorTex, v_uv);
    vec4 centre = texture2D(uTerrainDepthTex, v_uv);
    // Sky: nothing to outline, and the neighbour test would draw the horizon twice. uInkSky 1 runs it
    // anyway, as the reference does: its sky is depth 1, so the skyline is inked on BOTH sides.
    if (coverage(centre) < 0.5 && uInkSky < 0.5) {
        gl_FragColor = color;
        return;
    }
    // uOutlineWidth DILATES the one-pixel operator rather than spreading its taps: wider taps measure
    // a slope over more ground, which greyed the whole picture along with thickening the lines.
    float relative = operatorAt(v_uv);
    for (int ring = 1; ring < 4; ring++) {
        if (float(ring) >= uOutlineWidth) {
            break;
        }
        vec2 reach = uInvScreenSize * float(ring);
        relative = max(relative, max(max(operatorAt(v_uv + vec2(reach.x, 0.0)), operatorAt(v_uv - vec2(reach.x, 0.0))),
                                     max(operatorAt(v_uv + vec2(0.0, reach.y)), operatorAt(v_uv - vec2(0.0, reach.y)))));
    }
    // uOutlineFloor is a subtraction BEFORE the gain, and it is 0 by default - which is the
    // reference's behaviour and the look this mode is judged against. This operator is a gradient
    // MAGNITUDE, so the slope term it returns everywhere is not an artefact: it IS the hillshade,
    // and geo-three leans on exactly the same thing (its power is 0.23). Raising the floor turns it
    // into a pure edge detector - sharper lines, no wash - which reads as a different picture.
    // uOutlineCeiling is how far an edge may go past 1 before uIntensity scales it (unset reads as 1).
    // The reference's is 2: its outline is mixed into a TRANSPARENT terrain, alpha included, so its
    // ink is min(d * d * 0.5, 1) - a strong step reaches black even at its intensity of a half.
    float ceiling = uOutlineCeiling > 0.0 ? uOutlineCeiling : 1.0;
    float edge = min(pow(max((relative - uOutlineFloor) * uOutlineGain, 0.0), max(uOutlinePower, 0.01)), ceiling) * uIntensity;
    // THE SKYLINE, as a stroke of its own width and weight. A depth operator draws it no heavier
    // than an interior fold; coverage says which neighbour is sky. Terrain side only, so it adds to
    // the reference's two-sided skyline (uInkSky) rather than filling the sky.
    vec2 skyOffset = uInvScreenSize * max(uHorizonWidth, 1.0);
    float skyNeighbour = 1.0 - min(
        min(coverage(texture2D(uTerrainDepthTex, v_uv + vec2(skyOffset.x, 0.0))),
            coverage(texture2D(uTerrainDepthTex, v_uv - vec2(skyOffset.x, 0.0)))),
        min(coverage(texture2D(uTerrainDepthTex, v_uv + vec2(0.0, skyOffset.y))),
            coverage(texture2D(uTerrainDepthTex, v_uv - vec2(0.0, skyOffset.y)))));
    edge = clamp(max(edge, skyNeighbour * coverage(centre) * uHorizonBoost), 0.0, 1.0);
    if (uTransparent > 0.5) {
        // PREMULTIPLIED, and the labels over the lines, as in reliefOutlineShader.
        float inkAlpha = edge * uInkColor.a;
        gl_FragColor = color + vec4(uInkColor.rgb * inkAlpha, inkAlpha) * (1.0 - color.a);
        return;
    }
    gl_FragColor = vec4(mix(color.rgb, uInkColor.rgb, edge), color.a);
}
