
const vec4 uPaperColor = vec4(1, 1, 1, 1);
const vec4 uShadeColor = vec4(0, 0, 0, 1);
uniform float uShadeStrength;
uniform float uSlopeShade;
uniform float uAmbient;
uniform float uHillshade;
uniform float uDebugView;
// The most ink the light allows anywhere (theirs 0.3).
uniform float uInkCap;
// The RIDGE term: how far the surface normal turns across one screen pixel, which is peakfinder.com's
// interior line (the screen-space gradient of their normal buffer). Measured here off the elevation
// texture instead and scaled to the ground one pixel covers (v_dist * uPixelAngle, radians per pixel).
uniform float uRidgeInkStrength;
uniform float uPixelAngle;
// Bilinear by hand: the texture's own filter is not exact on a packed height (terrarium's R carries
// every 256 m), which put a ~1 m step on every 256 m contour - a spike for any derivative.
float exactHeightUv(vec2 uv) {
    vec2 texel = uv / u_demInvTexSize - 0.5;
    vec2 base = floor(texel);
    vec2 f = texel - base;
    vec2 at = (base + 0.5) * u_demInvTexSize;
    float h00 = terrainHeightUv(at);
    float h10 = terrainHeightUv(at + vec2(u_demInvTexSize.x, 0.0));
    float h01 = terrainHeightUv(at + vec2(0.0, u_demInvTexSize.y));
    float h11 = terrainHeightUv(at + u_demInvTexSize);
    return mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
}
float terrainTurn() {
    if (u_demValid < 0.5 || uRidgeInkStrength <= 0.0) {
        return 0.0;
    }
    // The curvature is measured over u_demNormalStep (TerrainOptions::normalSampleDistance): the
    // scale of relief worth a line, where a fine DEM taken texel by texel is mostly noise - and as
    // far as the texture's border of neighbour data reaches, so no tap reads past it.
    float equatorTexel = max(u_demMetersPerTexel, 0.0001);
    float stepTexels = max(u_demNormalStep / equatorTexel, 1.0);
    float mercatorY = v_worldPos.y * u_demMercatorYScale;
    float stepMetres = stepTexels * equatorTexel * 2.0 / (exp(mercatorY) + exp(-mercatorY));
    // In the texture's own uv (v_demUv): v_worldPos is too coarse in float for taps a texel apart.
    vec2 tapStep = vec2(stepTexels) * u_demInvTexSize;
    vec2 uv = v_demUv;
    float h = exactHeightUv(uv);
    float east = exactHeightUv(uv + vec2(tapStep.x, 0.0));
    float west = exactHeightUv(uv - vec2(tapStep.x, 0.0));
    float north = exactHeightUv(uv + vec2(0.0, tapStep.y));
    float south = exactHeightUv(uv - vec2(0.0, tapStep.y));
    float twist = exactHeightUv(uv + tapStep) - exactHeightUv(uv + vec2(tapStep.x, -tapStep.y))
                - exactHeightUv(uv + vec2(-tapStep.x, tapStep.y)) + exactHeightUv(uv - tapStep);
    // The hessian over the step, times the two-pixel span of their central difference.
    float hxx = (east - 2.0 * h + west) / stepMetres;
    float hyy = (north - 2.0 * h + south) / stepMetres;
    float hxy = twist / (4.0 * stepMetres);
    vec2 slope = vec2(east - west, north - south) / (2.0 * stepMetres);
    // Then to ONE PIXEL's worth of turn, as theirs is, so a ridge inks as it does on their screen.
    float pixelMetres = v_dist * uPixelAngle;
    return 2.0 * sqrt(hxx * hxx + 2.0 * hxy * hxy + hyy * hyy) / (1.0 + dot(slope, slope)) * pixelMetres / stepMetres;
}
// Set by TerrainRenderer::renderTiles, per tile, from the MESH: (gridSize, attribsRefined, demZoom).
uniform vec4 u_tileDebug;
vec4 surfaceColor() {
    // DEBUG 9 and 10: WHAT EACH TILE ACTUALLY IS, painted on it.
    //
    // Which per-tile property makes one tile lighter than the one beside it has been inferred four
    // times - mesh density, sample distance, DEM stretch, prefetch order - and each inference cost a
    // rebuild to disprove. These two views read the answer off the picture instead: whatever the
    // light tiles have in common is visible in one screenshot.
    //
    //   9:  MESH DENSITY. red 4 | orange 16 | yellow 32 | green 48 | cyan 64 | blue 96.
    //   10: NORMAL SOURCE and DEM zoom. RED tile = still on the cheap mesh-gradient stand-in,
    //       GREEN = DEM-refined normals; brightness rises with the DEM zoom it resolved (z4..z12).
    if (uDebugView > 8.5 && uDebugView < 9.5) {
        float cells = u_tileDebug.x;
        if (cells < 8.0) { return vec4(1.0, 0.0, 0.0, 1.0); }
        if (cells < 24.0) { return vec4(1.0, 0.5, 0.0, 1.0); }
        if (cells < 40.0) { return vec4(1.0, 1.0, 0.0, 1.0); }
        if (cells < 56.0) { return vec4(0.0, 0.8, 0.0, 1.0); }
        if (cells < 80.0) { return vec4(0.0, 0.8, 1.0, 1.0); }
        return vec4(0.0, 0.0, 1.0, 1.0);
    }
    // 13: ARE THIS TILE'S NORMALS STALE? Attribs are baked once and never recomputed, so a tile
    //     refined while standing on a coarse ancestor keeps those normals after its own grid lands -
    //     and views 9 and 10 both report it as healthy, because gridSize, refined and demZoom are all
    //     correct. Only the stored VALUES are old, and which tiles lose the race changes every run.
    //       GREEN  normals computed from the DEM zoom this tile has now
    //       YELLOW one level stale | ORANGE two | RED three or more
    //       BLACK  never refined
    // 13: TILE BOUNDARIES, over the real shading. Every per-tile property measured so far - gridSize,
    //     refined, demZoom, staleness - has come back uniform while regions still differ, so this
    //     tests the assumption under all of them: that the differing regions ARE tiles. Alternate
    //     tiles are darkened slightly, leaving the shading readable underneath. If the pale patches
    //     line up with the checker, they are tiles and something per-tile is still unmeasured; if
    //     they cut across it, they were never tiles and the whole per-tile search was misdirected.

    if (uDebugView > 9.5 && uDebugView < 10.5) {
        // DEM ZOOM AS BANDS, not as brightness. Encoded as brightness this read as "all green" even
        // where the shading plainly differed - a range of zooms is invisible against a colour ramp,
        // and an instrument that cannot be read is worse than none. One hue per zoom instead.
        //   z<=5 red | 6 orange | 7 yellow | 8 green | 9 cyan | 10 blue | 11 magenta | 12+ white
        // A tile with no grid at all (demZoom -1) is BLACK.
        float demZoom = u_tileDebug.z;
        if (demZoom < 0.0) { return vec4(0.0, 0.0, 0.0, 1.0); }
        if (demZoom < 5.5) { return vec4(1.0, 0.0, 0.0, 1.0); }
        if (demZoom < 6.5) { return vec4(1.0, 0.5, 0.0, 1.0); }
        if (demZoom < 7.5) { return vec4(1.0, 1.0, 0.0, 1.0); }
        if (demZoom < 8.5) { return vec4(0.0, 0.8, 0.0, 1.0); }
        if (demZoom < 9.5) { return vec4(0.0, 0.8, 1.0, 1.0); }
        if (demZoom < 10.5) { return vec4(0.0, 0.2, 1.0, 1.0); }
        if (demZoom < 11.5) { return vec4(1.0, 0.0, 1.0, 1.0); }
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
    // DEBUG 7: the slope this shader sees, straight off v_normal - the SAME per-vertex attribute
    // the normal-packing depth pass reads. Compared against debug 1, which reads the PACKED buffer,
    // it says which side of that pass is at fault: both black means the mesh attribute itself is
    // flat, only the packed one black means the packing pass is losing it.
    // Bounded above as well: view 8 is a post-process view, and the surface must draw normally
    // under it or there is nothing for the post-process to classify.
    if (uDebugView > 6.5 && uDebugView < 7.5) {
        float debugSlope = length(normalize(v_normal).xy);
        return vec4(debugSlope, debugSlope, debugSlope, 1.0);
    }
    // PER FRAGMENT, off the elevation texture (terrainNormal, supplied by the SDK's surface shader
    // prefix), not the mesh normal interpolated across a cell. A mesh carries one normal per cell
    // corner and a cell is hundreds of metres of ground, so every ridge narrower than that was
    // smoothed away before this shader ran - which is why the hillshade read soft next to
    // peakfinder's and no amount of shade-strength tuning closed the gap. Falls back to v_normal
    // wherever no elevation texture is bound yet.
    // 22: WHICH PATH THIS FRAGMENT TOOK, which is the only way to tell three states apart that all
    //     look like "wrong shading": RED no elevation texture (mesh-normal fallback), GREEN a skirt
    //     (keeps the edge normal), BLUE the per-fragment DEM normal.
    if (uDebugView > 21.5 && uDebugView < 22.5) {
        if (v_normal.z < 0.0) { return vec4(0.0, 1.0, 0.0, 1.0); }
        if (u_demValid < 0.5) { return vec4(1.0, 0.0, 0.0, 1.0); }
        return vec4(0.0, 0.0, 1.0, 1.0);
    }
    // A SKIRT is a crack filler, not a surface. It is a vertical wall hanging from a tile edge, and
    // it exists only so that the gap between two tiles at different levels is not see-through - it
    // is meant to be unnoticed, and while the tiles load it is the tallest thing on screen. Shading
    // it means lighting a vertical face with the normal of the ground above it, and adjacent
    // columns take adjacent edge vertices, so it bands vertically in hard black and white. Paper:
    // it fills the crack and says nothing.
    if (v_normal.z < 0.0) {
        return vec4(uPaperColor.rgb, 1.0);
    }
    // ONE SHADER FOR EVERY TILE. There is no mesh-normal path left to fall back to and no
    // paper stand-in for a tile whose elevation texture has not arrived: terrainNormal returns
    // flat until it does, so a loading tile shades as ground and then gains its relief. The
    // picture fills in; it never changes style.
    vec3 n = terrainNormal(u_demNormalStep);
    // 20: the DEM uv this fragment resolves to - red/green ramp inside [0,1], BLUE outside it. A
    //     fragment sampling outside its elevation texture reads the clamped edge, so all four taps
    //     return the same height and the normal comes out exactly vertical: a flat tile with a
    //     perfectly healthy texture bound.
    // 21: the scale the taps are spaced by. BLACK means u_demMetersPerTexel arrived as zero, which
    //     sends the step to infinity and produces the same flat result for a different reason.
    if (uDebugView > 19.5 && uDebugView < 20.5) {
        vec2 uv = (v_worldPos.xy - u_demOriginSize.xy) / u_demOriginSize.zw;
        if (u_demValid < 0.5) { return vec4(1.0, 0.0, 1.0, 1.0); }
        bool inside = uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
        return inside ? vec4(uv, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0);
    }
    if (uDebugView > 20.5 && uDebugView < 21.5) {
        float scaled = clamp(u_demMetersPerTexel / 200.0, 0.0, 1.0);
        return vec4(scaled, scaled, scaled, 1.0);
    }
    // PEAKFINDER.COM'S MODEL (peakfinder-reference-shader.md): the ink is the slope and the ridge
    // terms ADDED, and then CAPPED by the light - so a face turned to the sun stays paper whatever its
    // relief, and only the shadow side shows its gullies. The cap is what keeps the ground white at
    // every distance, where adding the light instead greys everything out.
    // Theirs at cfg=es: ridge 0.6, slope 0, sun 0.05, cap 0.3 (u_fragmentParams1/2, read off the page).
    vec3 sun = normalize(u_sunDir);
    float value = uSlopeShade * length(n.xy) + uRidgeInkStrength * terrainTurn();
    float light = min(uAmbient + uShadeStrength * max(-0.2, -dot(n, sun)), 1.0);
    value = min(value, min(light, uInkCap));
    // THE HILLSHADE, on top of the cap: how much less sun a face gets than flat ground does, so flat
    // ground stays paper, a face towards the sun stays paper, and a face turned away shades by how far
    // it is turned - the hillshade layer's reading, and it moves with the sun's azimuth.
    value = clamp(value + uHillshade * max(sun.z - dot(n, sun), 0.0), -1.0, 1.0);
    vec3 color = clamp(mix(uPaperColor.rgb, uShadeColor.rgb, value), 0.0, 1.0);

    // 13: TILE BOUNDARIES OVER THE REAL SHADING. Applied at the end, not as an early return, so the
    // picture is the actual one with a checker laid over it. Every per-tile property measured so far
    // - gridSize, refined, demZoom, staleness - is uniform while regions still differ, so this tests
    // the assumption underneath all of them: that the differing regions ARE tiles. If the pale
    // patches line up with the checker they are tiles and something per-tile is still unmeasured; if
    // they cut across it, they never were, and the per-tile search was misdirected from the start.
    if (uDebugView > 12.5 && uDebugView < 13.5 && u_tileDebug.w > 0.5) {
        color *= 0.75;
    }
    return vec4(color, 1.0);
}