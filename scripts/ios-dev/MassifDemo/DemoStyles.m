#import "DemoStyles.h"
#import "DemoConfig.h"
#import "DemoMap.h"

@implementation DemoStyles

+ (NSString *)landcoverOpacity {
    float opacity = [DemoConfig floatFor:@"landcoverOpacity"];
    // Opaque by default; translucent when the hillshade and contours underneath have to read
    // through (tangram's 'translucent-polygons').
    return opacity >= 1.0f ? @"" : [NSString stringWithFormat:@" polygon-opacity: %g;", opacity];
}

+ (NSString *)compOp {
    NSString *op = [DemoConfig stringFor:@"compOp"];
    return op.length ? [NSString stringWithFormat:@" polygon-comp-op: %@;", op] : @"";
}

+ (NSString *)join:(NSArray<NSString *> *)lines {
    return [lines componentsJoinedByString:@"\n"];
}

+ (NSString *)inlineStyle {
    NSMutableString *map = [NSMutableString stringWithFormat:@"Map { background-color: %@;",
                            [DemoConfig stringFor:@"bg"]];
    if ([DemoConfig boolFor:@"styleLight"]) {
        // The same sun/shadow/fog values the code sets on LightOptions/TerrainOptions, but
        // expressed IN the style - and zoom-dependent, which only the style can do.
        [map appendString:@" terrain-lighting: 1;"];
        [map appendString:@" sun-azimuth: 250;"];
        [map appendString:@" sun-altitude: linear([view::zoom], (11, 55), (15, 12));"];
        [map appendString:@" sun-intensity: 1;"];
        [map appendString:@" ambient-intensity: 0.4;"];
        [map appendFormat:@" building-light-intensity: %g;", [DemoConfig floatFor:@"bldLight"]];
        [map appendFormat:@" building-ambient: %g;", [DemoConfig floatFor:@"bldAmbient"]];
        [map appendString:@" shadow-strength: 0.8;"];
        [map appendString:@" shadow-softness: 1;"];
        [map appendString:@" fog-color: #b8c6d8;"];
        [map appendString:@" fog-start-distance: 1500;"];
        [map appendString:@" fog-distance: linear([view::zoom], (11, 60000), (15, 12000));"];
        [map appendString:@" terrain-max-visible-distance: 40000;"];
    }
    [map appendString:@" }"];

    NSString *satelliteSlot = [NSString stringWithFormat:
        @"#satellite[zoom>=%d] { raster-opacity: 1; raster-comp-op: src-over; }",
        [DemoConfig intFor:@"satZoom"]];
    NSString *hillshadeSlot = [self join:@[
        @"#hillshade[zoom>=4][zoom<=19] {",
        [NSString stringWithFormat:@"  hillshade-illumination-direction: %d;",
            (int)[DemoConfig floatFor:@"hsIllumination"]],
        @"  hillshade-shadow-color: #473B24;",
        [DemoConfig boolFor:@"hsContours"] ? [self join:@[
            [NSString stringWithFormat:@"  hillshade-contour-interval: %d;",
                (int)[DemoConfig floatFor:@"hsContourInterval"]],
            @"  hillshade-contour-width: 0.8;",
            @"  hillshade-contour-color: #FFC56008;"]] : @"",
        @"}"]];

    if ([DemoConfig boolFor:@"minimal"]) {
        // Background plus the composite slots only: no vector geometry, so a frame costs the
        // terrain and the slots and nothing else. The slot blocks have to stay - a source's
        // position in the draw order IS the position of the first rule naming it.
        return [self join:@[map, hillshadeSlot, satelliteSlot]];
    }

    BOOL labels = [DemoConfig boolFor:@"labels"];
    float labelMaxDistance = [DemoConfig floatFor:@"labelMaxDistance"];

    return [self join:@[
        map,
        @"#water { polygon-fill: #9cc3e0; }",
        [NSString stringWithFormat:@"#landuse { polygon-fill: #dddddd;%@ }", [self landcoverOpacity]],
        [NSString stringWithFormat:@"#landcover { polygon-fill: #dbe8cc;%@%@ }",
            [self landcoverOpacity], [self compOp]],
        // --- composite slots, in draw order ---
        satelliteSlot,
        hillshadeSlot,
        [self join:@[
            [NSString stringWithFormat:@"#contour[zoom>=%d] {", [DemoConfig intFor:@"contourMinZoom"]],
            // Lines only for the traced geometry: a label stub is a short fragment of a contour,
            // long enough to lay text along and nothing more, so drawing it as a line paints
            // dashes over the map.
            @"  [stub=0] {",
            @"    line-color: #C56008;",
            [NSString stringWithFormat:@"    line-width: %@;", [DemoConfig stringFor:@"contourWidth"]],
            @"  }",
            [NSString stringWithFormat:@"  contour-base-interval: %d;",
                (int)[DemoConfig floatFor:@"contourInterval"]],
            [DemoConfig boolFor:@"contourStubs"] ? [self join:@[
                @"  contour-label-stubs: 1;",
                [NSString stringWithFormat:@"  contour-label-interval: %d;",
                    (int)[DemoConfig floatFor:@"contourStubInterval"]]]] : @"",
            @"}"]],
        [NSString stringWithFormat:@"#transportation { line-color: #ffffff; line-width: %@; }",
            [DemoConfig stringFor:@"roadWidth"]],
        labels ? [self join:@[
            @"#transportation_name {",
            @"  text-name: [name];",
            @"  text-fill: #000000;",
            @"  text-spacing: 10;",
            @"  text-placement: line;",
            @"  text-size: 10;",
            labelMaxDistance > 0
                ? [NSString stringWithFormat:@"  text-max-distance: %g;", labelMaxDistance] : @"",
            @"}"]] : @"",
        [NSString stringWithFormat:
            @"#transportation['class'='motorway'] { line-color: #e27d60; line-width: %@; }",
            [DemoConfig stringFor:@"motorwayWidth"]],
        [DemoConfig boolFor:@"bld3d"]
            ? [NSString stringWithFormat:
                @"#building[zoom>=14] { building-fill: #d9cfc4; building-height: %g; }",
                [DemoConfig floatFor:@"bldHeight"]]
            : @"#building[zoom>=14] { polygon-fill: #d9cfc4; }",
    ]];
}

+ (NSString *)contourStyle {
    return [self join:@[
        @"Map { }",
        [NSString stringWithFormat:@"#contour[zoom>=%d] {", [DemoConfig intFor:@"contourMinZoom"]],
        @"  [stub=0] { line-color: #C56008; line-width: 0.8; }",
        [NSString stringWithFormat:@"  contour-base-interval: %d;",
            (int)[DemoConfig floatFor:@"contourInterval"]],
        @"}"]];
}

+ (NSString *)contourTilesStyle {
    return [self join:@[
        @"Map { }",
        @"#contour {",
        [NSString stringWithFormat:@"  line-color: #C56008; line-width: %@;",
            [DemoConfig stringFor:@"contourWidth"]],
        @"}"]];
}

+ (NSString *)routeTestStyle {
    // Casing under the line, both from the same source, which is what makes the join and cap
    // knobs visible.
    return [self join:@[
        @"Map { }",
        @"#route::case {",
        [NSString stringWithFormat:@"  line-color: %@;", [DemoConfig stringFor:@"routeCaseColor"]],
        [NSString stringWithFormat:@"  line-width: %g;", [DemoConfig floatFor:@"routeCaseWidth"]],
        [NSString stringWithFormat:@"  line-join: %@;", [DemoConfig stringFor:@"routeJoin"]],
        [NSString stringWithFormat:@"  line-cap: %@;", [DemoConfig stringFor:@"routeCap"]],
        @"}",
        @"#route::line {",
        [NSString stringWithFormat:@"  line-color: %@;", [DemoConfig stringFor:@"routeColor"]],
        [NSString stringWithFormat:@"  line-width: %g;", [DemoConfig floatFor:@"routeWidth"]],
        [NSString stringWithFormat:@"  line-join: %@;", [DemoConfig stringFor:@"routeJoin"]],
        [NSString stringWithFormat:@"  line-cap: %@;", [DemoConfig stringFor:@"routeCap"]],
        [NSString stringWithFormat:@"  line-opacity: %g;", [DemoConfig floatFor:@"routeOpacity"]],
        @"}"]];
}

+ (NSString *)slopesShader {
    // Bands rather than a gradient: the point is reading "is this skiable", not a smooth ramp.
    return [self join:@[
        @"uniform vec4 u_shadowColor;",
        @"uniform vec4 u_highlightColor;",
        @"uniform vec4 u_accentColor;",
        @"uniform vec3 u_lightDir;",
        @"vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {",
        @"    mediump float lighting = max(0.0, dot(normal, u_lightDir));",
        @"    mediump float slope = acos(dot(normal, surfaceNormal)) * 180.0 / 3.14159 * 1.2;",
        @"    if (slope >= 45.0) { return vec4(0.378, 0.272, 0.358, 0.5); }",
        @"    if (slope >= 40.0) { return vec4(0.5, 0.0, 0.0, 0.5); }",
        @"    if (slope >= 35.0) { return vec4(0.455, 0.231, 0.111, 0.5); }",
        @"    if (slope >= 30.0) { return vec4(0.470, 0.451, 0.153, 0.5); }",
        @"    return vec4(0.0, 0.0, 0.0, 0.0);",
        @"}"]];
}

+ (NSString *)hypsometricShader {
    // Reads the RAW DEM texel rather than the shaded colour, which is what shows that the
    // custom-raster base class can run any filter over any raster source.
    return [self join:@[
        @"vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {",
        @"  vec4 c = getRawColor();",
        @"  float h = (c.r * 255.0 * 256.0 + c.g * 255.0 + c.b * 255.0 / 256.0) - 32768.0;",
        @"  float t = clamp(h / 3000.0, 0.0, 1.0);",
        @"  vec3 col = mix(vec3(0.2, 0.4, 0.8), vec3(0.9, 0.9, 0.4), t);",
        @"  col = mix(col, vec3(0.5, 0.3, 0.1), clamp((h - 1500.0) / 1500.0, 0.0, 1.0));",
        @"  return vec4(col, 1.0);",
        @"}"]];
}

+ (NSString *)reliefSurfaceShader {
    return [self join:@[
        @"uniform vec4 uPaperColor;",
        @"uniform vec4 uShadeColor;",
        @"uniform float uShadeStrength;",
        @"uniform float uAmbient;",
        @"uniform float uHaze;",
        @"uniform float uHazeDistance;",
        @"vec4 surfaceColor() {",
        @"    vec3 n = normalize(v_normal);",
        @"    float lambert = max(dot(n, normalize(u_sunDir)), 0.0);",
        @"    float light = mix(uAmbient, 1.0, lambert);",
        @"    vec3 color = mix(uShadeColor.rgb, uPaperColor.rgb, clamp(1.0 - uShadeStrength * (1.0 - light), 0.0, 1.0));",
        @"    color = mix(color, uPaperColor.rgb, clamp(v_dist / max(uHazeDistance, 1.0), 0.0, 1.0) * uHaze);",
        // No fog here: the SDK applies the frame's own fog to whatever surfaceColor returns.
        @"    return vec4(color, 1.0);",
        @"}"]];
}

/**
 * Summit names, drawn as callout labels: the label is lifted to a band near the top of the screen
 * and joined back to the summit by a leader line, and a label that would collide is moved one row
 * up instead of being dropped ('callout' placement). The layer name and fields are
 * OpenMapTiles ('mountain_peak', name/ele/class).
 */
+ (NSString *)peaksStyle {
    // The leader line always meets the FIRST letter of the name, which is also the point held over
    // the summit. What changes with the mode is the corner the row is aligned on: pinned to the top
    // the labels hang from their top right corner so the text stays under the screen edge; in a
    // band lower down they line up on the same bottom left corner they are anchored by.
    BOOL pinTop = [DemoConfig boolFor:@"peaksPinTop"];
    NSString *lineAnchor = [DemoConfig stringFor:@"peaksLineAnchor"].length
        ? [DemoConfig stringFor:@"peaksLineAnchor"] : @"bottom-left";
    NSString *align = [DemoConfig stringFor:@"peaksAlign"].length
        ? [DemoConfig stringFor:@"peaksAlign"] : (pinTop ? @"top-right" : @"bottom-left");
    float minDistance = [DemoConfig floatFor:@"peaksMinDistance"];
    float distanceRank = [DemoConfig floatFor:@"peaksDistanceRank"];
    float maxDistance = [DemoConfig floatFor:@"peaksMaxDistance"];
    float step = [DemoConfig floatFor:@"peaksStep"];

    return [self join:@[
        @"Map { }",
        [NSString stringWithFormat:@"#mountain_peak['class'='peak'][zoom>=%d] {",
            [DemoConfig intFor:@"peaksMinZoom"]],
        @"  text-name: [name];",
        // The elevation as a second run of text: same label, same plate, smaller font.
        @"  text-secondary-name: [ele]+'m';",
        [NSString stringWithFormat:@"  text-secondary-scale: %g;", [DemoConfig floatFor:@"peaksEleScale"]],
        [NSString stringWithFormat:@"  text-secondary-fill: %@;", [DemoConfig stringFor:@"peaksEleColor"]],
        [NSString stringWithFormat:@"  text-secondary-dx: %g;", [DemoConfig floatFor:@"peaksEleGap"]],
        [NSString stringWithFormat:@"  text-secondary-dy: %g;", [DemoConfig floatFor:@"peaksEleDy"]],
        [NSString stringWithFormat:@"  text-size: %g;", [DemoConfig floatFor:@"peaksTextSize"]],
        // The names follow the relief palette, so they stay readable in both.
        [NSString stringWithFormat:@"  text-fill: %@;", [DemoMap reliefInk]],
        [NSString stringWithFormat:@"  text-halo-fill: %@;", [DemoMap reliefPaper]],
        @"  text-halo-radius: 1.5;",
        // The plate behind the name - a general label property, so a classic map style can use
        // exactly the same four lines.
        [NSString stringWithFormat:@"  text-background-fill: %@;",
            [DemoConfig boolFor:@"reliefDark"] ? [DemoConfig stringFor:@"reliefPaperDark"]
                                               : [DemoConfig stringFor:@"peaksBgColor"]],
        [NSString stringWithFormat:@"  text-background-opacity: %g;", [DemoConfig floatFor:@"peaksBgOpacity"]],
        [NSString stringWithFormat:@"  text-background-radius: %g;", [DemoConfig floatFor:@"peaksBgRadius"]],
        [NSString stringWithFormat:@"  text-background-padding-x: %g;", [DemoConfig floatFor:@"peaksBgPaddingX"]],
        [NSString stringWithFormat:@"  text-background-padding-y: %g;", [DemoConfig floatFor:@"peaksBgPaddingY"]],
        @"  text-placement: callout;",
        // The higher summit claims the row: without this the winner is whichever label the tile
        // order happened to offer first, and a 700 m hill hides a 2000 m one behind it.
        @"  text-placement-priority: [ele];",
        minDistance > 0 ? [NSString stringWithFormat:@"  text-min-distance: %g;", minDistance] : @"",
        // ... and the nearer of two summits of the same height wins the slot. No feature field in
        // the expression on purpose: one that reads only the view state is built ONCE and shared by
        // every label. '0 - x', not '-x': a leading minus in front of a field parses as a literal.
        distanceRank > 0
            ? [NSString stringWithFormat:@"  text-rank: [ele] + [view::distance]/%g;", distanceRank] : @"",
        [NSString stringWithFormat:@"  text-orientation: %g;", [DemoConfig floatFor:@"peaksAngle"]],
        [NSString stringWithFormat:@"  text-callout-line-anchor: %@;", lineAnchor],
        [NSString stringWithFormat:@"  text-callout-align: %@;", align],
        [NSString stringWithFormat:@"  text-callout-screen-anchor: %g;",
            pinTop ? [DemoConfig floatFor:@"peaksTopOffset"] : [DemoConfig floatFor:@"peaksBand"]],
        [NSString stringWithFormat:@"  text-callout-offset: %g;", [DemoConfig floatFor:@"peaksOffset"]],
        // Pinned to the top there is no room above the row, so the extra rows go DOWN.
        [NSString stringWithFormat:@"  text-callout-step: %g;", pinTop ? -step : step],
        [NSString stringWithFormat:@"  text-callout-max-rows: %d;", [DemoConfig intFor:@"peaksRows"]],
        [NSString stringWithFormat:@"  text-callout-persist: %d;", [DemoConfig intFor:@"peaksPersist"]],
        [NSString stringWithFormat:@"  text-callout-line-width: %g;", [DemoConfig floatFor:@"peaksLineWidth"]],
        maxDistance > 0 ? [NSString stringWithFormat:@"  text-max-distance: %g;", maxDistance] : @"",
        @"}"]];
}

// =================================================================================================
// MANEUVER ARROWS
// =================================================================================================

/** width at maneuverZoomRef, minScale of it at maneuverZoomMin, interpolated in between. */
+ (NSString *)maneuverWidthByZoom:(float)width {
    return [NSString stringWithFormat:@"linear([view::zoom], (%g, %g), (%g, %g))",
            [DemoConfig floatFor:@"maneuverZoomMin"], width * [DemoConfig floatFor:@"maneuverMinScale"],
            [DemoConfig floatFor:@"maneuverZoomRef"], width];
}

/**
 * What to multiply the fill's arrow numbers by for the casing rule, so the head keeps the border
 * the shaft has. The head's inradius is r = a*L / (a + hypot(a, L)) for a half-base a and a length
 * L; the casing head is the fill head grown by (casing - fill) / 2, which for a triangle is the
 * same shape scaled about its incenter - and the numbers are read against the casing's own, wider
 * line, hence the width ratio.
 */
+ (float)maneuverCasingArrowScale {
    float fill = [DemoConfig floatFor:@"maneuverWidth"];
    float casing = [DemoConfig floatFor:@"maneuverCaseWidth"];
    if (fill <= 0 || casing <= fill) {
        return 1;
    }
    double a = [DemoConfig floatFor:@"maneuverArrowWidth"] * fill / 2;
    double l = [DemoConfig floatFor:@"maneuverArrowLength"] * fill;
    double inradius = a * l / (a + hypot(a, l));
    double grown = inradius + (casing - fill) / 2;
    return (float)(grown / inradius * fill / casing);
}

+ (NSString *)maneuverArrow:(float)arrowWidth length:(float)arrowLength path:(NSString *)headPath {
    // A custom path replaces the built-in triangle. It is a SKELETON, offset outward by half of
    // each rule's own line width, so BOTH rules use the same path and the casing lands
    // (casing - fill) / 2 outside the fill - the border the shaft has, for any shape. The built-in
    // triangle keeps its own route (grown about its incenter), which is the same offset on a
    // triangle and needs no path at all.
    // In path mode the two numbers are the BOX the contour is fitted into - length along the line,
    // width across it - so they still drive the size, and the path can come from any viewBox.
    NSString *shape = headPath.length == 0
        ? [NSString stringWithFormat:@" line-arrow-width: %g; line-arrow-length: %g;", arrowWidth, arrowLength]
        : [NSString stringWithFormat:@" line-arrow-width: %g; line-arrow-length: %g;"
                                     @" line-arrow-scale: %g; line-arrow-rotation: %g;"
                                     @" line-arrow-path: '%@';",
           arrowWidth, arrowLength, [DemoConfig floatFor:@"maneuverPathScale"],
           [DemoConfig floatFor:@"maneuverPathRotation"], headPath];
    return [@" line-join: round; line-cap: round; line-end-arrow: true; line-arrow-only: true;"
            stringByAppendingString:shape];
}

/**
 * ManeuverArrowBuilder serves one LINE per arrow; the head is 'line-end-arrow', which the vt line
 * tesselator builds on the last vertex out of the same screen-space extrusion the line itself uses.
 *
 * That is why the casing works: the casing rule repeats the arrow properties with its own, wider
 * line, and the head grows about its incenter - so the border is as thick round the head as it is
 * along the shaft, and shaft and head are ONE shape with no seam between them.
 *
 * Everything scales together with the camera: the widths are interpolated over [view::zoom] - the
 * LIVE camera zoom, re-evaluated every frame, not the tile's - and the head is a multiple of the
 * width, so the arrow keeps its shape instead of swallowing the junction as the map zooms out.
 */
+ (NSString *)maneuverStyle:(NSString *)headPath {
    float width = [DemoConfig floatFor:@"maneuverWidth"];
    float casing = [DemoConfig floatFor:@"maneuverCaseWidth"];
    float arrowWidth = [DemoConfig floatFor:@"maneuverArrowWidth"];
    float arrowLength = [DemoConfig floatFor:@"maneuverArrowLength"];
    // Both modes need the casing's numbers scaled back, because they are read against ITS wider
    // line. With a custom path the box is what scales, so the correction is just the ratio of the
    // widths; the built-in triangle needs the incenter formula instead, because its numbers
    // describe the drawn shape rather than a skeleton.
    float scale = headPath.length == 0 ? [self maneuverCasingArrowScale]
                                       : width / MAX(1.0e-3f, casing);

    NSMutableString *mss = [NSMutableString string];
    // Whole SHAFT first, then the head over it, each part in its own attachment - an attachment is
    // drawn at the position of its FIRST rule. The head paints over the line, so it keeps its
    // outline where it lands on its own shaft (a U-turn, once the map is zoomed out enough) instead
    // of dissolving into it; and 'line-arrow-only' cuts a slot one line width wide out of the
    // head's base, so the two read as a single polygon.
    if (casing > 0) {
        [mss appendFormat:@"#maneuver::case { line-color: %@; line-width: %@;"
                          @" line-join: round; line-cap: round; }\n",
         [DemoConfig stringFor:@"maneuverCaseColor"], [self maneuverWidthByZoom:casing]];
    }
    [mss appendFormat:@"#maneuver::fill { line-color: %@; line-width: %@;"
                      @" line-join: round; line-cap: round; }\n",
     [DemoConfig stringFor:@"maneuverColor"], [self maneuverWidthByZoom:width]];
    if (casing > 0) {
        [mss appendFormat:@"#maneuver::headcase { line-color: %@; line-width: %@;%@ }\n",
         [DemoConfig stringFor:@"maneuverCaseColor"], [self maneuverWidthByZoom:casing],
         [self maneuverArrow:arrowWidth * scale length:arrowLength * scale path:headPath]];
    }
    [mss appendFormat:@"#maneuver::head { line-color: %@; line-width: %@;%@ }",
     [DemoConfig stringFor:@"maneuverColor"], [self maneuverWidthByZoom:width],
     [self maneuverArrow:arrowWidth length:arrowLength path:headPath]];
    return mss;
}

+ (NSString *)poiTestStyle {
    // A shield per label: the ICON stays on the feature and the NAME goes on whichever side the
    // culler finds free, falling back to the icon alone when none is.
    return [self join:@[
        @"Map { }",
        @"#poi {",
        @"  shield-name: [name];",
        @"  shield-size: 11;",
        @"  shield-fill: #333333;",
        @"  shield-halo-fill: #ffffff;",
        @"  shield-halo-radius: 1.5;",
        [NSString stringWithFormat:@"  shield-anchors: '%@';", [DemoConfig stringFor:@"poiAnchors"]],
        [NSString stringWithFormat:@"  shield-text-optional: %d;", [DemoConfig boolFor:@"poiTextOptional"] ? 1 : 0],
        [NSString stringWithFormat:@"  shield-dx: %g;", [DemoConfig floatFor:@"poiTextDx"]],
        [NSString stringWithFormat:@"  shield-wrap-width: %g;", [DemoConfig floatFor:@"poiWrapWidth"]],
        @"}"]];
}

/**
 * The relief (peak-finder) OUTLINE effect, as a fragment shader for PostProcessEffect: silhouettes
 * and creases reconstructed from the packed terrain depth the renderer hands the effect. It lives
 * here, not in the SDK, for the same reason the surface shader does - the SDK provides the
 * mechanism (an offscreen frame, a depth texture, named parameters) and the application decides
 * what the map looks like.
 * Parameters: uIntensity, uOutlineWidth, uHorizonBoost, uDepthThreshold, uCreaseStrength,
 * uDepthTexelSize, uGrazingFloor, uDistanceFade, uHaze, uInkColor, uPaperColor.
 */
+ (NSString *)reliefOutlineShader {
    return [self join:@[
        @"#version 100",
        @"#ifdef GL_FRAGMENT_PRECISION_HIGH",
        @"precision highp float;",
        @"#else",
        @"precision mediump float;",
        @"#endif",
        @"",
        @"uniform sampler2D uColorTex;",
        @"uniform sampler2D uTerrainDepthTex;",
        @"uniform vec2 uInvScreenSize;",
        @"uniform vec2 uProjInvScale;",
        @"uniform float uFar;",
        @"uniform float uIntensity;",
        @"uniform float uOutlineWidth;",
        @"uniform float uHorizonBoost;",
        @"uniform float uDepthThreshold;",
        @"uniform float uCreaseStrength;",
        @"uniform float uDepthTexelSize;",
        @"uniform float uGrazingFloor;",
        @"uniform float uDistanceFade;",
        @"uniform float uHaze;",
        @"uniform vec4 uInkColor;",
        @"uniform vec4 uPaperColor;",
        @"",
        @"float unpackDepth(vec4 c) {",
        @"    return dot(c.rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0));",
        @"}",
        @"",
        @"// Eye-space position of a pixel from the packed linear depth.",
        @"vec3 eyePos(vec2 uv, float depth) {",
        @"    vec2 ndc = uv * 2.0 - 1.0;",
        @"    return vec3(ndc * uProjInvScale, -1.0) * depth * uFar;",
        @"}",
        @"",
        @"void main(void) {",
        @"    vec2 uv = gl_FragCoord.xy * uInvScreenSize;",
        @"    vec4 color = texture2D(uColorTex, uv);",
        @"",
        @"    vec4 c0 = texture2D(uTerrainDepthTex, uv);",
        @"    float d0 = unpackDepth(c0);",
        @"",
        @"    // One width for the terrain-against-terrain lines, everywhere. Widening them with",
        @"    // distance instead (the obvious reading of \"the horizon is bolder\") smears the",
        @"    // far ranges into a solid band: up there the ridges are a pixel apart, so every",
        @"    // pixel is inside some line. What is bold in a panorama is the SKY silhouette,",
        @"    // and that gets its own, wider test below.",
        @"    // Never narrower than uDepthTexelSize screen pixels: the terrain depth runs at",
        @"    // half resolution with nearest filtering, so a narrower step samples the same",
        @"    // texel twice and every comparison below degenerates.",
        @"    vec2 delta = uInvScreenSize * max(uOutlineWidth, uDepthTexelSize);",
        @"    vec2 skyDelta = uInvScreenSize * max(uOutlineWidth * (1.0 + uHorizonBoost), uDepthTexelSize);",
        @"    vec4 cx0 = texture2D(uTerrainDepthTex, uv - vec2(delta.x, 0.0));",
        @"    vec4 cx1 = texture2D(uTerrainDepthTex, uv + vec2(delta.x, 0.0));",
        @"    vec4 cy0 = texture2D(uTerrainDepthTex, uv - vec2(0.0, delta.y));",
        @"    vec4 cy1 = texture2D(uTerrainDepthTex, uv + vec2(0.0, delta.y));",
        @"    float dx0 = unpackDepth(cx0);",
        @"    float dx1 = unpackDepth(cx1);",
        @"    float dy0 = unpackDepth(cy0);",
        @"    float dy1 = unpackDepth(cy1);",
        @"",
        @"    // The local surface, from the four neighbours. Two things below need it: a",
        @"    // surface seen edge-on legitimately changes depth fast from pixel to pixel, and a",
        @"    // fold has to be told apart from a merely oblique slope.",
        @"    vec3 p0 = eyePos(uv, d0);",
        @"    vec3 tx0 = eyePos(uv - vec2(delta.x, 0.0), dx0) - p0;",
        @"    vec3 tx1 = eyePos(uv + vec2(delta.x, 0.0), dx1) - p0;",
        @"    vec3 ty0 = eyePos(uv - vec2(0.0, delta.y), dy0) - p0;",
        @"    vec3 ty1 = eyePos(uv + vec2(0.0, delta.y), dy1) - p0;",
        @"    // Two samples that landed on the same depth texel give a zero tangent, and",
        @"    // normalizing that is undefined - it painted the whole near field grey.",
        @"    float minLength = 1.0e-4 * d0 * uFar;",
        @"    bool tangentsValid = length(tx1) > minLength && length(ty1) > minLength;",
        @"    float grazing = 1.0;",
        @"    if (tangentsValid) {",
        @"        vec3 surfaceNormal = normalize(cross(tx1, ty1));",
        @"        grazing = abs(dot(normalize(-p0), surfaceNormal));",
        @"    }",
        @"",
        @"    // Silhouette: the line belongs to the NEARER side of a depth break, so only a",
        @"    // neighbour FURTHER away counts. Testing the absolute difference draws the same",
        @"    // ridge twice, once on each side, which at the horizon merges into a smear.",
        @"    // The threshold is relative to the depth, or the far half of the view draws",
        @"    // no line at all - and it is relaxed where the surface is seen EDGE-ON, because",
        @"    // there the depth runs away between neighbouring pixels without anything being",
        @"    // in front of anything: flat ground at its own horizon drew a solid black band.",
        @"    float behind = max(max(dx0 - d0, dx1 - d0), max(dy0 - d0, dy1 - d0));",
        @"    float threshold = uDepthThreshold * (0.0008 + 0.02 * d0) / max(grazing, uGrazingFloor);",
        @"    float edge = smoothstep(threshold, threshold * 2.0, behind);",
        @"    // Terrain-against-terrain lines fade with distance so that the horizon - the sky",
        @"    // silhouette below, which does not fade - is the boldest line in the frame.",
        @"    edge *= mix(1.0, uDistanceFade, d0);",
        @"    // ...and terrain against the sky always is one (coverage, not depth: a sky pixel",
        @"    // is at the far plane, which the relative threshold above would forgive). This is",
        @"    // the horizon line, and it is the one that is drawn wide.",
        @"    float skyNeighbour = 1.0 - min(",
        @"        min(texture2D(uTerrainDepthTex, uv - vec2(skyDelta.x, 0.0)).a, texture2D(uTerrainDepthTex, uv + vec2(skyDelta.x, 0.0)).a),",
        @"        min(texture2D(uTerrainDepthTex, uv - vec2(0.0, skyDelta.y)).a, texture2D(uTerrainDepthTex, uv + vec2(0.0, skyDelta.y)).a));",
        @"    edge = max(edge, skyNeighbour * c0.a);",
        @"",
        @"    // Ridges and valleys: the two tangent directions away from this pixel point",
        @"    // straight apart on a flat surface (dot -1) and fold together over a crest.",
        @"    // Done on eye positions rather than on depth, so a merely oblique slope - which",
        @"    // is most of a panorama - does not read as a fold.",
        @"    float cover = min(min(cx0.a, cx1.a), min(cy0.a, cy1.a)) * c0.a;",
        @"    if (uCreaseStrength > 0.0 && cover > 0.0) {",
        @"        float fold = 0.0;",
        @"        if (length(tx0) > minLength && length(tx1) > minLength) {",
        @"            fold = max(fold, 1.0 + dot(normalize(tx0), normalize(tx1)));",
        @"        }",
        @"        if (length(ty0) > minLength && length(ty1) > minLength) {",
        @"            fold = max(fold, 1.0 + dot(normalize(ty0), normalize(ty1)));",
        @"        }",
        @"        // Same reasoning as the silhouette threshold: an edge-on surface folds in",
        @"        // projection without folding in the world.",
        @"        edge = max(edge, smoothstep(0.05, 0.4, fold) * uCreaseStrength * grazing * mix(1.0, uDistanceFade, d0));",
        @"    }",
        @"",
        @"    // Aerial perspective: the shaded surface fades into the paper with distance, so",
        @"    // the far ranges read as pale outlines and the near ground keeps its shading.",
        @"    vec3 shaded = mix(color.rgb, uPaperColor.rgb, uHaze * d0 * c0.a);",
        @"    vec3 stylized = mix(shaded, uInkColor.rgb, edge * uInkColor.a);",
        @"",
        @"    gl_FragColor = vec4(mix(color.rgb, stylized, uIntensity), 1.0);",
        @"}"]];
}

// =================================================================================================
// STYLE PARAMETER STYLE
// 'param::' parameters are user settings the style reacts to at runtime (decoder.setStyleParameter).
// They can only be DECLARED in a style project, so the project is built in memory here:
// project.json + style.mss, zipped, wrapped in a CompiledStyleSet.
// =================================================================================================

+ (NSString *)boolParameter {
    return @"show_relief";
}

+ (MSFMBVectorTileDecoder *)createProjectDecoder {
    NSString *parameter = [self boolParameter];
    // 'layers' is TOP -> BOTTOM (reversed into draw order) and must list every composite slot.
    NSString *projectJson = [self join:@[
        @"{",
        @"  \"styles\": [\"style.mss\"],",
        @"  \"layers\": [\"contour\", \"building\", \"transportation\", \"satellite\", \"hillshade\", \"landcover\", \"water\"],",
        [NSString stringWithFormat:@"  \"styleparameters\": { \"%@\": { \"default\": true } }", parameter],
        @"}"]];
    NSString *mss = [self join:@[
        [NSString stringWithFormat:@"Map { background-color: %@; }", [DemoConfig stringFor:@"bg"]],
        @"#water { polygon-fill: #9cc3e0; }",
        [NSString stringWithFormat:@"#landcover { polygon-fill: #dbe8cc;%@ }", [self landcoverOpacity]],
        // the hillshade slot exists only while the user setting is on
        [NSString stringWithFormat:@"#hillshade['param::%@'=true][zoom>=4] {", parameter],
        @"  hillshade-opacity: linear([view::zoom], (4, 0.5), (12, 0.9));",
        @"  hillshade-exaggeration: linear([view::zoom], (4, 0.6), (12, 1.4));",
        @"  hillshade-illumination-direction: 315;",
        @"  hillshade-shadow-color: #103040;",
        @"}",
        [NSString stringWithFormat:@"#satellite[zoom>=%d] { raster-opacity: 0.45; }",
            [DemoConfig intFor:@"satZoom"]],
        @"#contour[zoom>=12] { line-color: #9a5a12; line-width: 0.8; line-opacity: 0.7; }",
        @"#transportation { line-color: #ffffff; line-width: 1.2; }",
        [NSString stringWithFormat:
            @"#transportation['class'='motorway'] { line-color: #e27d60; line-width: %@; }",
            [DemoConfig stringFor:@"motorwayWidth"]],
        [DemoConfig boolFor:@"bld3d"]
            ? [NSString stringWithFormat:
                @"#building[zoom>=14] { building-fill: #d9cfc4; building-height: %g; }",
                [DemoConfig floatFor:@"bldHeight"]]
            : @"#building[zoom>=14] { polygon-fill: #d9cfc4; }"]];

    NSMutableData *zip = [self zipWithEntries:@{ @"project.json": projectJson, @"style.mss": mss }];
    if (!zip) {
        NSLog(@"MassifDemo: could not build the style project");
        return nil;
    }
    MSFBinaryData *data = [[MSFBinaryData alloc] initWithDataPtr:(unsigned char *)zip.bytes
                                                          size:(unsigned int)zip.length];
    MSFZippedAssetPackage *package = [[MSFZippedAssetPackage alloc] initWithZipData:data];
    return [[MSFMBVectorTileDecoder alloc] initWithCompiledStyleSet:
            [[MSFCompiledStyleSet alloc] initWithAssetPackage:package]];
}

/**
 * A minimal STORED (uncompressed) zip. Foundation has no zip writer, and the whole archive here is
 * two small text files, so deflate would buy nothing - the SDK's reader accepts stored entries.
 */
+ (NSMutableData *)zipWithEntries:(NSDictionary<NSString *, NSString *> *)entries {
    NSMutableData *out = [NSMutableData data];
    NSMutableData *directory = [NSMutableData data];
    uint16_t count = 0;

    for (NSString *name in entries) {
        NSData *nameBytes = [name dataUsingEncoding:NSUTF8StringEncoding];
        NSData *content = [entries[name] dataUsingEncoding:NSUTF8StringEncoding];
        uint32_t crc = [self crc32OfData:content];
        uint32_t offset = (uint32_t)out.length;

        uint8_t local[30] = {0};
        uint32_t signature = 0x04034b50;
        memcpy(local, &signature, 4);
        local[4] = 20;                                  // version needed
        local[8] = 0;                                   // method 0 = stored
        memcpy(local + 14, &crc, 4);
        uint32_t size = (uint32_t)content.length;
        memcpy(local + 18, &size, 4);                   // compressed size
        memcpy(local + 22, &size, 4);                   // uncompressed size
        uint16_t nameLength = (uint16_t)nameBytes.length;
        memcpy(local + 26, &nameLength, 2);
        [out appendBytes:local length:sizeof(local)];
        [out appendData:nameBytes];
        [out appendData:content];

        uint8_t entry[46] = {0};
        uint32_t centralSignature = 0x02014b50;
        memcpy(entry, &centralSignature, 4);
        entry[4] = 20;                                  // version made by
        entry[6] = 20;                                  // version needed
        entry[10] = 0;                                  // method 0 = stored
        memcpy(entry + 16, &crc, 4);
        memcpy(entry + 20, &size, 4);
        memcpy(entry + 24, &size, 4);
        memcpy(entry + 28, &nameLength, 2);
        memcpy(entry + 42, &offset, 4);
        [directory appendBytes:entry length:sizeof(entry)];
        [directory appendData:nameBytes];
        count++;
    }

    uint32_t directoryOffset = (uint32_t)out.length;
    [out appendData:directory];

    uint8_t end[22] = {0};
    uint32_t endSignature = 0x06054b50;
    memcpy(end, &endSignature, 4);
    memcpy(end + 8, &count, 2);
    memcpy(end + 10, &count, 2);
    uint32_t directorySize = (uint32_t)directory.length;
    memcpy(end + 12, &directorySize, 4);
    memcpy(end + 16, &directoryOffset, 4);
    [out appendBytes:end length:sizeof(end)];
    return out;
}

+ (uint32_t)crc32OfData:(NSData *)data {
    static uint32_t table[256];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
    });
    const uint8_t *bytes = data.bytes;
    uint32_t crc = 0xffffffffu;
    for (NSUInteger i = 0; i < data.length; i++) {
        crc = table[(crc ^ bytes[i]) & 0xff] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

+ (MSFMBVectorTileDecoder *)createDecoder {
    NSString *source = [DemoConfig stringFor:@"style"];

    if ([source isEqualToString:@"project"]) {
        MSFMBVectorTileDecoder *decoder = [self createProjectDecoder];
        if (decoder) {
            return decoder;
        }
        NSLog(@"MassifDemo: falling back to the inline style");
    }

    if ([source isEqualToString:@"zip"] || [source isEqualToString:@"assets"]) {
        // A style zip bundled with the app, mirroring the Android demo's osm.zip path.
        NSString *path = [[NSBundle mainBundle] pathForResource:@"osm" ofType:@"zip"];
        if (path) {
            NSData *bytes = [NSData dataWithContentsOfFile:path];
            MSFBinaryData *data = [[MSFBinaryData alloc] initWithDataPtr:(unsigned char *)bytes.bytes
                                                                  size:(unsigned int)bytes.length];
            MSFZippedAssetPackage *package = [[MSFZippedAssetPackage alloc] initWithZipData:data];
            return [[MSFMBVectorTileDecoder alloc] initWithCompiledStyleSet:
                    [[MSFCompiledStyleSet alloc] initWithAssetPackage:package]];
        }
        NSLog(@"MassifDemo: no osm.zip in the bundle, falling back to the inline style");
    }

    MSFCartoCSSStyleSet *styleSet = [[MSFCartoCSSStyleSet alloc] initWithCartoCSS:[self inlineStyle]];
    return [[MSFMBVectorTileDecoder alloc] initWithCartoCSSStyleSet:styleSet];
}

@end
