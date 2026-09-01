package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * Everything the sky and the fog can do, on one map: scattering, a day cycle, stars, and peaks
 * standing clear of a valley haze.
 */
@ExampleInfo(
    id = "atmosphere",
    title = "Sky, fog and the day cycle",
    description = "A physical atmosphere over 3D terrain: dawn, noon, dusk and night, with the fog "
                + "lit by the same sun, stars beyond it, and summits standing clear of the haze "
                + "filling the valley.",
    section = Sections.TERRAIN,
    order = 20)
public class AtmosphereExample extends MapExample {

    /** Identifies the app to the tile servers, which both of them ask for. */
    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** Looking south at the Matterhorn from over Zermatt - see Terrain3DExample for the framing. */
    private static final Position VIEW = new Position(7.6586, 45.9763);

    /**
     * One row per hour of interest: label, hour, and the colours the atmosphere is not responsible
     * for - the FOG's own tint and the sky exposure. Everything else (sun position, the light on
     * the ground, the colour the haze is lit to) follows from the hour.
     */
    private static final Object[][] MOMENTS = {
        //  label     hour  fog colour   high colour  space colour  stars  exposure
        { "Dawn",     6.5f, 0xffd8b48c,  0xffe08a5a,  0x66202a4a,   0.25f, 1.6f },
        { "Noon",    13.0f, 0xffb8c6d8,  0x00000000,  0x00000000,   0.00f, 1.0f },
        { "Dusk",    19.5f, 0xffc98a63,  0xffe06a3a,  0x88141c38,   0.35f, 1.8f },
        { "Night",   23.0f, 0xff1a2338,  0xff101a34,  0xff05070f,   0.90f, 3.2f },
    };

    private int moment = 2; // start at dusk: it is what shows the scattering off best
    private boolean cycling;
    private boolean atmosphere = true;
    private boolean customSky;
    private float hour = (Float) MOMENTS[2][1];
    private ExampleHost host;

    /** Open DEM tiles, terrarium-encoded, cached on disk in front of the server. */
    private static Spec dem(ExampleHost host) {
        return Spec.of("persistent-cache")
            .set("databasePath", host.cachePath("mapterhorn-dem.db"))
            .set("capacity", 200 * 1024 * 1024)
            .set("source", Spec.of("http")
                .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                .set("minZoom", 1)
                .set("maxZoom", 16)
                    // What picks the elevation decoder, per tile - see Terrain3DExample for why
                    // this is the whole map and why it stays on the HTTP source.
                .set("metaData", Spec.object().set("dem_encoding", "terrarium")));
    }

    @Override
    public void onStart(ExampleHost exampleHost) {
        this.host = exampleHost;
        final MassifMap map = host.map();

        map.addLayer("satellite", Spec.of("raster")
            // Cached on disk in front of the server: imagery tiles are big, and a demo that gets
            // panned around re-fetches the same ones on every run.
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("world-imagery.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://server.arcgisonline.com/ArcGIS/rest/services/"
                              + "World_Imagery/MapServer/tile/{z}/{y}/{x}")
                    .set("maxZoom", 18)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));

        map.style("hybrid", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/hybrid.zip")))));
        map.addLayer("labels", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "hybrid"));

        map.terrain(Spec.of("terrain").set("source", dem(host)))
           .apply(Spec.object()
               .set("exaggeration", 1)
               .set("viewDistanceFactor", 1.6)
               .set("cameraClearance", 40));

        // The sky. ATMOSPHERE is the default, so this spec only names what differs from it - the
        // exposure the moment wants is set below, with everything else the hour drives.
        map.sky(Spec.of("sky").set("sunDiscEnabled", true));

        // The fog. Its range is in multiples of the camera-to-focus distance, so one pair holds at
        // every zoom. horizonBlend is what carries the haze up into the sky - and the GROUND is
        // scaled by the same term, which is why the two meet along the skyline with no seam.
        map.fog(Spec.of("fog")
            .set("rangeStart", 1.4)
            .set("rangeEnd", 7.0)
            .set("horizonBlend", 0.22)
            // The summits stand clear of the haze filling the valley (mapbox vertical-range).
            .set("verticalRangeStart", 1800)
            .set("verticalRangeEnd", 3200));

        map.light(Spec.of("light")
            .set("terrainLightingEnabled", true)
            .set("shadowStrength", 1.0)
            .set("shadowSoftness", 1.5));

        applyMoment();
        map.camera().moveTo(VIEW, 11.5f, 180f, 33f);

        host.button("Time", new Runnable() {
            @Override
            public void run() {
                moment = (moment + 1) % MOMENTS.length;
                hour = (Float) MOMENTS[moment][1];
                applyMoment();
            }
        });
        host.toggle("Run the day", false, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                cycling = on;
                if (on) {
                    step();
                }
            }
        });
        // The A/B for both the look and the cost: GRADIENT is the two-colour ramp the SDK drew
        // before the atmosphere, and it ignores every Atmosphere* property.
        host.toggle("Atmosphere", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                atmosphere = on;
                applySky();
            }
        });
        host.toggle("Comets & clouds", false, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                customSky = on;
                applySky();
                applyMoment();
            }
        });
        host.toggle("Peaks above the fog", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // Equal values disable the vertical fade, so the haze fills the whole view again.
                map.fog().set("verticalRangeStart", on ? 1800 : 0);
                map.fog().set("verticalRangeEnd", on ? 3200 : 0);
            }
        });
        host.toggle("Fog", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // Enabled is a real switch: nothing has to be driven through zero and back.
                map.fog().set("enabled", on);
            }
        });
    }

    @Override
    public void onStop() {
        cycling = false;
    }

    /** Advances the clock while the toggle is on. The host cancels the callback for us. */
    private void step() {
        if (!cycling) {
            return;
        }
        hour = (hour + 0.25f) % 24f;
        applyHour();
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                step();
            }
        }, 100);
    }

    /** The picked moment: its hour, plus the colours the model does not derive. */
    private void applyMoment() {
        MassifMap map = host.map();
        Object[] m = MOMENTS[moment];
        map.fog().apply(Spec.object()
            .set("color", m[2])
            .set("highColor", m[3])
            .set("spaceColor", m[4])
            .set("starIntensity", m[5]));
        map.sky().set("atmosphereLuminance", m[6]);
        applyHour();
        host.caption(m[0] + " - " + captionFor(moment));
    }

    /**
     * Everything the hour drives. Sun position from a deliberately crude model: this example is
     * about the sky, not about ephemerides, and the demo bench has the real one.
     *
     * The fog is NOT tinted here - resolveFog lights the configured colour with the same sun the
     * ground gets, so a fog tuned for daylight darkens through the night on its own. Neither is
     * the shadow strength: the SDK scales it by how much of the light is direct, so it fades out
     * as the sun sets without the hour touching it.
     */
    private void applyHour() {
        double altitude = 62.0 * Math.sin(Math.PI * (hour - 6.0) / 12.0);
        double azimuth = 90.0 + (hour - 6.0) * 15.0;
        // Below the horizon there is no sun to light anything with, and the ambient is what keeps
        // the map readable at all.
        double sunUp = Math.max(0.0, Math.min(1.0, altitude / 8.0));
        host.map().light().apply(Spec.object()
            .set("sunAzimuth", azimuth)
            .set("sunAltitude", altitude)
            .set("sunIntensity", 0.15 + 0.85 * sunUp)
            .set("ambientIntensity", 0.45 - 0.15 * sunUp));
    }

    /**
     * Owns both sky switches, because they are not independent: the custom shader calls
     * atmosphere(), which the SDK only compiles in under SKY_TYPE_ATMOSPHERE. Leaving the two
     * toggles to set the type separately would let a user reach GRADIENT with the custom source
     * still attached, and a shader that names a function nobody declared does not fail loudly - it
     * falls back to the built-in sky and logs, which reads as "my shader does nothing".
     */
    private void applySky() {
        MassifMap map = host.map();
        if (customSky) {
            map.sky().apply(Spec.object()
                .set("type", "SKY_TYPE_ATMOSPHERE")
                .set("shaderSource", CUSTOM_SKY));
        } else {
            map.sky().apply(Spec.object()
                .set("shaderSource", "")
                .set("type", atmosphere ? "SKY_TYPE_ATMOSPHERE" : "SKY_TYPE_GRADIENT"));
        }
    }

    /**
     * A custom sky: the SDK's own scattering, with a cloud deck lit by the sun and, once the sun is
     * down, comets crossing it.
     *
     * The contract is one function, vec4 skyColor(vec3 rayDir), returning the NON-premultiplied
     * colour. Everything it reads is already declared by the wrapper - redeclaring any of it is a
     * compile error - and it must NOT fog itself: the SDK applies the frame's own haze to whatever
     * this returns, so the sky still meets the ground at the skyline and a custom FOG shader still
     * reaches it.
     */
    private static final String CUSTOM_SKY = String.join("\n",
        "float hash21(vec2 p) {",
        "  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);",
        "}",
        "float valueNoise(vec2 p) {",
        "  vec2 i = floor(p), f = fract(p);",
        "  f = f * f * (3.0 - 2.0 * f);",
        "  return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),",
        "             mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);",
        "}",
        "",
        "// A flat cloud deck the ray is projected onto: cheap, and the perspective is right.",
        "// The octaves are ROTATED against each other - stacking axis-aligned value noise",
        "// leaves the grid visible, which reads as soft squares once the sun lights them.",
        "float clouds(vec3 dir) {",
        "  if (dir.z <= 0.02) return 0.0;",
        "  vec2 p = dir.xy / dir.z * 1.7 + vec2(u_time * 0.004, 0.0);",
        "  mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);",
        "  float f = 0.50 * valueNoise(p);",
        "  p = rot * p * 2.1; f += 0.25 * valueNoise(p);",
        "  p = rot * p * 2.3; f += 0.15 * valueNoise(p);",
        "  p = rot * p * 2.7; f += 0.10 * valueNoise(p);",
        "  // Thinned towards the horizon, where the deck would be edge-on and solid.",
        "  return smoothstep(0.46, 0.74, f) * smoothstep(0.02, 0.25, dir.z);",
        "}",
        "",
        "// Three comets, laid out in (azimuth, elevation) so a streak keeps its width near the",
        "// horizon - a flat projection smears it into a band there. Each one crosses on its",
        "// own stagger, head first, with the tail BEHIND it.",
        "float comets(vec3 dir) {",
        "  vec2 sky = vec2(atan(dir.y, dir.x), asin(clamp(dir.z, -1.0, 1.0)));",
        "  float total = 0.0;",
        "  for (int i = 0; i < 3; i++) {",
        "    float fi = float(i);",
        "    float t = fract(u_time * 0.07 + fi * 0.37);",
        "    vec2 from = vec2(-2.6 + fi * 0.7, 1.25);",
        "    vec2 to = vec2(1.4 + fi * 0.5, 0.18);",
        "    vec2 head = mix(from, to, t);",
        "    vec2 axis = normalize(to - from);",
        "    vec2 d = sky - head;",
        "    float along = dot(d, axis);",
        "    float across = length(d - axis * along);",
        "    float tail = exp(-across * across / 0.00012)",
        "                 * smoothstep(-0.5, 0.0, along) * step(along, 0.0);",
        "    float glow = exp(-dot(d, d) / 0.00006);",
        "    // Faded in and out over the pass, so nothing pops at the edge of the frame.",
        "    total += (tail * 0.55 + glow) * smoothstep(0.0, 0.12, t) * smoothstep(1.0, 0.86, t);",
        "  }",
        "  return total;",
        "}",
        "",
        "vec4 skyColor(vec3 rayDir) {",
        "  float elevation = asin(clamp(rayDir.z, -1.0, 1.0));",
        "  // Below the horizon is the wedge between the last terrain tile and the mathematical",
        "  // horizon. The wrapper has the right answer for it, coverage included.",
        "  if (elevation < 0.0) return groundBelowHorizon(rayDir);",
        "",
        "  // The SDK's own scattering, tonemapped the same way the built-in sky tonemaps it.",
        "  vec3 scattered = atmosphere(rayDir, u_sunDir) * (8.0 / u_atmosphere.y);",
        "  vec3 col = tonemap(scattered) / tonemap(vec3(11.2));",
        "  col = atmosphereTint(col, elevation);",
        "",
        "  // How much of a day it is, from the sun's own altitude - no clock is passed in.",
        "  float day = clamp(u_sunDir.z * 4.0 + 0.15, 0.0, 1.0);",
        "",
        "  // Clouds take the sun's colour where it strikes them and stay grey where it does not.",
        "  float cover = clouds(rayDir);",
        "  vec3 lit = mix(vec3(0.55, 0.58, 0.66), u_sunColor.rgb * 1.05, day);",
        "  col = mix(col, lit, cover * (0.30 + 0.55 * day));",
        "",
        "  // Comets and stars only once the sun is down, and added rather than mixed: they are",
        "  // lights, not surfaces.",
        "  col += vec3(0.80, 0.88, 1.0) * comets(rayDir) * (1.0 - day) * (1.0 - cover);",
        "  col += vec3(starAmount(rayDir, elevation)) * (1.0 - cover);",
        "",
        "  return sunDisc(vec4(col, 1.0), rayDir);",
        "}");

    private String captionFor(int index) {
        if (customSky) {
            return "custom sky: clouds by day, comets once the sun is down";
        }
        switch (index) {
            case 0: return "the sun just up, the haze warm and low";
            case 1: return "high sun, thin blue sky, almost no haze";
            case 3: return "the sun gone, stars beyond the atmosphere";
            default: return "a low sun reddens the whole sky, not just the disc";
        }
    }
}
