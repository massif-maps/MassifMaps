package com.massifmaps.MassifDemo.demo;

import com.massifmaps.components.LightOptions;
import com.massifmaps.components.SkyOptions;
import com.massifmaps.graphics.Color;

/**
 * Day-cycle demo: sun position, sky/horizon/ground colours, shadow strength and a GENERATED sky
 * shader that draws the sun disc, the moon, stars and procedural clouds - all from one hour value.
 *
 * Nothing here touches layers, only Options, so it can be switched on and off at runtime.
 *
 * The moon direction and the sun's arc are baked into the shader SOURCE instead of being passed as
 * uniforms: the sky shader contract has a fixed uniform set, and regenerating the source when the
 * hour changes is cheap enough for a demo.
 */
public final class DemoSky {

    /**
     * Applies one hour of the day cycle. lat/lon should be the current map centre, since the sun
     * position is computed for it.
     */
    public static void applyHour(LightOptions light, SkyOptions sky, float hourUtc, double lat, double lon) {
        if (light == null || sky == null) {
            return;
        }
        light.setSunPositionFromTime(DemoConfig.SUN_YEAR, DemoConfig.SUN_MONTH, DemoConfig.SUN_DAY,
                (int) hourUtc, (int) ((hourUtc - (int) hourUtc) * 60), lat, lon);
        float altitude = light.getSunAltitude();

        // day = 1 well above the horizon, 0 below it, with civil twilight in between.
        float day = Math.max(0f, Math.min(1f, (altitude + 6f) / 12f));
        float warm = 1f - Math.max(0f, Math.min(1f, altitude / 25f)); // reddening near the horizon

        light.setSunColor(new Color(
                (short) 255,
                (short) (int) (255 - 90 * warm),
                (short) (int) (255 - 190 * warm),
                (short) 255));
        light.setSunIntensity(0.15f + 0.85f * day);
        light.setAmbientIntensity(0.25f + 0.55f * day);
        light.setShadowStrength(DemoConfig.SHADOW_STRENGTH); // the SDK fades it with the sun itself

        int skyR = (int) (10 + 48 * day), skyG = (int) (14 + 102 * day), skyB = (int) (40 + 156 * day);
        int horR = (int) (25 + (146 + 60 * warm) * day), horG = (int) (25 + 181 * day), horB = (int) (55 + 181 * day);
        sky.setSkyColor(new Color((short) skyR, (short) skyG, (short) skyB, (short) 255));
        sky.setHorizonColor(new Color((short) horR, (short) horG, (short) horB, (short) 255));
        sky.setGroundColor(new Color((short) (horR * 0.8), (short) (horG * 0.8), (short) (horB * 0.8), (short) 255));

        // The sun's daily path is a circle; three positions on it define its plane.
        double[] a = sunVectorAt(6, lat, lon), b = sunVectorAt(12, lat, lon), c = sunVectorAt(18, lat, lon);
        double[] u = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        double[] v = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        double[] n = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
        double nlen = Math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nlen > 1e-9) {
            n[0] /= nlen;
            n[1] /= nlen;
            n[2] /= nlen;
        }

        // The moon rides roughly the opposite side of the same arc, offset by the monthly phase.
        double[] moon = sunVectorAt((hourUtc + 12.7f) % 24f, lat, lon);

        sky.setShaderSource(buildSkyShader(n, moon, day, hourUtc));
    }

    /** Unit vector of the sun at a given hour, in the renderer's east/north/up frame. */
    private static double[] sunVectorAt(float hourUtc, double lat, double lon) {
        LightOptions probe = new LightOptions();
        int hour = (int) hourUtc;
        int minute = (int) ((hourUtc - hour) * 60);
        probe.setSunPositionFromTime(DemoConfig.SUN_YEAR, DemoConfig.SUN_MONTH, DemoConfig.SUN_DAY, hour, minute, lat, lon);
        double az = Math.toRadians(probe.getSunAzimuth());
        double alt = Math.toRadians(probe.getSunAltitude());
        double cosAlt = Math.cos(alt);
        return new double[] { cosAlt * Math.sin(az), cosAlt * Math.cos(az), Math.sin(alt) };
    }

    private static String formatVec(double[] v) {
        return String.format(java.util.Locale.US, "vec3(%.5f, %.5f, %.5f)", v[0], v[1], v[2]);
    }

    private static String buildSkyShader(double[] arcNormal, double[] moonDir, float day, float hourUtc) {
        // Cloud cover and layout change with the hour: the seed is derived from it, so scrubbing
        // the slider rolls a different (but stable) sky.
        float seed = (hourUtc * 7.13f) % 10.0f;
        float cover = 0.35f + 0.25f * (float) Math.sin(hourUtc * 0.7f);
        // The sky shader wrapper already declares u_sunDir/u_sunColor/u_skyColor/u_horizonColor/
        // u_groundColor/u_time and the whole fog block - redeclaring any of them is a compile error
        // and the renderer silently falls back to the built-in sky.
        return String.join("\n",
            "const vec3 ARC_N = " + formatVec(arcNormal) + ";",
            "const vec3 MOON_DIR = " + formatVec(moonDir) + ";",
            String.format(java.util.Locale.US, "const float SEED = %.4f;", seed),
            String.format(java.util.Locale.US, "const float COVER = %.4f;", cover),
            String.format(java.util.Locale.US, "const float DAY = %.4f;", day),
            "",
            "float hash(vec2 p) {",
            "  return fract(sin(dot(p, vec2(127.1, 311.7)) + SEED) * 43758.5453);",
            "}",
            "float noise(vec2 p) {",
            "  vec2 i = floor(p), f = fract(p);",
            "  f = f * f * (3.0 - 2.0 * f);",
            "  return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),",
            "             mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);",
            "}",
            "float clouds(vec3 dir) {",
            "  if (dir.z <= 0.02) return 0.0;",
            "  // Project the ray onto a flat cloud deck: cheap, and the perspective is right.",
            "  // The octaves are rotated against each other - stacking axis-aligned value noise",
            "  // leaves the grid visible, which reads as soft squares once the sun lights them.",
            "  vec2 p = dir.xy / dir.z * 1.7 + vec2(u_time * 0.004, 0.0);",
            "  mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);",
            "  float f = 0.50 * noise(p);",
            "  p = rot * p * 2.1; f += 0.25 * noise(p);",
            "  p = rot * p * 2.3; f += 0.15 * noise(p);",
            "  p = rot * p * 2.7; f += 0.10 * noise(p);",
            "  float c = smoothstep(COVER, COVER + 0.28, f);",
            "  return c * smoothstep(0.02, 0.25, dir.z); // fade them out at the horizon",
            "}",
            "",
            "vec4 skyColor(vec3 rayDir) {",
            "  vec3 dir = normalize(rayDir);",
            "  float h = clamp(dir.z, -1.0, 1.0);",
            "  vec3 col = h < 0.0",
            "      ? mix(u_horizonColor.rgb, u_groundColor.rgb, clamp(-h * 6.0, 0.0, 1.0))",
            "      : mix(u_horizonColor.rgb, u_skyColor.rgb, pow(clamp(h, 0.0, 1.0), 0.45));",
            "",
            "  // Stars, only once the sky is dark enough to see them. The cells are laid out in",
            "  // (azimuth, elevation) - a flat projection (dir.xy / dir.z) stretches them into",
            "  // long streaks near the horizon, where dir.z goes to zero.",
            "  if (DAY < 0.55 && h > 0.0) {",
            "    vec2 sc = vec2(atan(dir.y, dir.x), asin(clamp(h, -1.0, 1.0))) * 320.0;",
            "    vec2 cell = floor(sc);",
            "    float pick = hash(cell);",
            "    // One star per cell at most, placed inside it and drawn as a soft dot - a whole",
            "    // lit cell reads as a grey square at this size.",
            "    vec2 pos = vec2(hash(cell + 1.7), hash(cell + 5.3));",
            "    float d = length(fract(sc) - pos);",
            "    float star = step(0.982, pick) * smoothstep(0.34, 0.02, d) * (0.4 + 0.6 * fract(pick * 37.0));",
            "    col += vec3(star * (0.55 - DAY) * 1.7 * smoothstep(0.0, 0.10, h));",
            "  }",
            "",
            "  col = mix(col, vec3(1.0, 1.0, 0.98), clouds(dir) * (0.35 + 0.5 * DAY));",
            "",
            "  // No fog here: the SDK applies the SAME haze to the sky as to the ground, once,",
            "  // after skyColor returns - see FogOptions.",
            "",
            "  // Sun: disc, then glow, tinted toward the sun colour rather than added, so a bright",
            "  // sky does not saturate to white far from it.",
            "  float ds = length(dir - normalize(u_sunDir));",
            "  col = mix(col, u_sunColor.rgb, clamp(1.0 - smoothstep(0.0, 0.12, ds), 0.0, 1.0) * 0.85);",
            "  col = mix(col, u_sunColor.rgb * 1.15, (1.0 - smoothstep(0.0, 0.03, ds)));",
            "",
            "  // Moon: a small disc with a soft halo, brighter as the sky darkens.",
            "  float dm = length(dir - normalize(MOON_DIR));",
            "  float moonLit = 0.35 + 0.65 * (1.0 - DAY);",
            "  col = mix(col, vec3(0.86, 0.88, 0.92), (1.0 - smoothstep(0.0, 0.020, dm)) * moonLit);",
            "  col = mix(col, vec3(0.70, 0.74, 0.85), (1.0 - smoothstep(0.02, 0.09, dm)) * 0.18 * moonLit);",
            "",
            "  return vec4(col, 1.0);",
            "}",
            "");
    }

    private DemoSky() {
    }
}
