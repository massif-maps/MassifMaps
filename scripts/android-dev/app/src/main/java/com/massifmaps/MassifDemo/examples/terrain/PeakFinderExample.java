package com.massifmaps.MassifDemo.examples.terrain;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MapEvents;
import com.massifmaps.api.Massif;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Position;
import com.massifmaps.api.PropertyGroup;
import com.massifmaps.api.Spec;

import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import java.util.Scanner;

/**
 * The web peak finder (web/examples/peak-finder.mjs) with the host's buttons in place of its
 * chrome. The shaders and the summit CartoCSS are assets, written by scripts/gen-peak-finder-assets.mjs.
 */
@ExampleInfo(
    id = "peak-finder",
    title = "Peak finder",
    description = "A panorama drawn as peakfinder.com draws it: the terrain as ink on paper from a "
                + "shader, every summit named along the skyline, and the sun's path with its rise "
                + "and set over the ridges. Drag to look around, tap a name, then fly to it.",
    section = Sections.TERRAIN,
    order = 90)
public class PeakFinderExample extends MapExample {

    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";
    private static final String DEM = "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp";

    /** The ink pass: silhouettes only (operator 2), a heavier skyline. Uniforms left out read zero. */
    private static final Object[][] INK = {
        { "uOperator", 2 }, { "uOutlineGain", 12 }, { "uOutlinePower", 1 }, { "uOutlineFloor", 0.008 },
        { "uOutlineCeiling", 1 }, { "uOutlineWidth", 1 }, { "uIntensity", 0.8 }, { "uHorizonBoost", 0.9 },
        { "uHorizonWidth", 2.5 }, { "uDepthThreshold", 1 }, { "uCreaseThreshold", 0.12 },
        { "uRidgeStrength", 2 }, { "uRidgeThreshold", 0.05 }, { "uRidgeGroundSpan", 90 },
        { "uDepthTexelSize", 2 }, { "uGrazingFloor", 0.15 }, { "uInkDistance", 50000 },
        { "uMetersPerUnit", 40075016.68558 / (1 << 20) }, { "uSilhouetteGate", 865 },
        { "uHazeDistance", 60000 }, { "uDistortCenterX", 0.5 }, { "uDistortCenterY", 0.5 },
        { "uDistortScreenTanX", 1 }, { "uDistortScreenTanY", 1 }, { "uDistortRenderTanX", 1 },
        { "uDistortRenderTanY", 1 },
    };
    /** The surface: ridge ink capped by the light, and a touch of hillshade after the cap. */
    private static final Object[][] SURFACE = {
        { "uAmbient", 0.06 }, { "uInkCap", 0.3 }, { "uRidgeInkStrength", 0.3 }, { "uHillshade", 0.15 },
    };

    private ExampleHost host;
    private MassifMap map;
    private PropertyGroup terrain;
    private MassifObject view;
    private MassifSource dem;
    private PeakFinderSky sky;
    private String summitsCss;

    // The viewpoint: Grenoble, 400 m up, looking east at Belledonne - rotation is minus the heading.
    private double lat;
    private double lon;
    private double eye;
    private double rotation;
    private double tilt;
    private double ground;
    private boolean hours;

    private int generation;
    private Peak selected;
    private String riseSet = "";
    private String shown = "";

    private static final class Peak {
        final String name;
        final String ele;
        final Position position;

        Peak(String name, String ele, Position position) {
            this.name = name;
            this.ele = ele;
            this.position = position;
        }
    }

    @Override
    public void onStart(ExampleHost exampleHost) {
        host = exampleHost;
        map = host.map();
        lat = host.option("lat", 45.1885f);
        lon = host.option("lon", 5.7245f);
        eye = host.option("elevation", 400);
        rotation = host.option("rotation", -100);
        tilt = host.option("tilt", -4);
        summitsCss = asset("summits.mss");

        dem = map.source("dem", Spec.of("persistent-cache")
            .set("databasePath", host.cachePath("mapterhorn-dem.db"))
            .set("capacity", 200 * 1024 * 1024)
            .set("source", Spec.of("http")
                .set("url", DEM)
                .set("maxZoom", 16)
                .set("metaData", Spec.object().set("dem_encoding", "terrarium"))));

        // The terrain, and everything decided when its meshes are built: geo-three's cut (subdivide
        // distance 70, levels to 17) and mesh, no stitching, drawn 173 km out.
        terrain = map.terrain(Spec.of("terrain")
            .set("source", "dem")
            .set("autoFlattenTilt", 0)
            .set("autoFlattenParallax", 0)
            .set("meshResolution", 171)
            .set("tileEdgeStitchingEnabled", false)
            .set("subdivideDistance", 70)
            .set("maxZoom", 17)
            .set("viewDistance", 173000)
            .set("meshCacheSize", 640)
            .set("normalSampleDistance", 40)
            .set("postProcessDownscale", 1)
            // The picture is the surface shader, so nothing is draped over it.
            .set("surfaceShaderSource", asset("relief-surface.glsl"))
            .set("backgroundColor", "#ffffff")
            .set("sharedGroundEnabled", false)
            .set("drapeFillsEnabled", false)
            .set("drapeLinesEnabled", false)
            .set("billboardOcclusionEnabled", true)
            .set("billboardOcclusionTolerance", 0.15)
            .set("maxTileZoomCoarsening", 4));
        for (Object[] parameter : SURFACE) {
            map.options().call("terrainOptions.setSurfaceParameter", parameter[0], parameter[1]).close();
        }

        // The ink is a post-process over the frame, reading the terrain's depth. The renderer hangs
        // off the map VIEW, which the map registered under its own id.
        MassifObject ink = map.object("effect", "relief", Spec.of("postprocess")
            .set("name", "relief")
            .set("fragmentShader", asset("relief-ink.glsl"))
            .set("terrainDepthRequired", true));
        for (Object[] parameter : INK) {
            ink.call("setFloatParameter", parameter[0], parameter[1]).close();
        }
        view = Massif.find("view", map.options().id());
        view.set("mapRenderer.postProcessEffect", ink);

        // No sky, no background pattern: the panorama is read against the paper.
        map.apply(Spec.object()
            .set("skyColor", "#00000000")
            .set("clearColor", "#ffffff")
            .set("labelPadding", 200));
        map.set("backgroundBitmap", null);
        map.light(Spec.of("light").set("sunAzimuth", 315).set("sunAltitude", 45));

        // First person: the position IS the eye and a drag turns the view about it. A panorama looks
        // at the horizon, so the tilt range opens above it too (tilt 90 is straight down).
        map.apply(Spec.object()
            .set("freeRoamMode", "FREE_ROAM_MODE_FIRST_PERSON")
            .set("tiltRange", new double[] { -90, 90 }));
        setFov(host.option("fov", 46));
        placeCamera();

        // THE SUMMIT NAMES: OpenMapTiles' mountain_peak, and a style that puts every name in one
        // row above the skyline. The sky's path goes under them, the sun and its times over them.
        map.source("peaks", Spec.of("persistent-cache")
            .set("databasePath", host.cachePath("openfreemap.db"))
            .set("capacity", 100 * 1024 * 1024)
            .set("source", Spec.of("http")
                .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                .set("maxZoom", 14)
                .set("HTTPHeaders", Spec.object().set("User-Agent", UA))));
        MassifLayer skyBelow = map.addLayer("sky", Spec.of("celestial")).moveTo(0);
        MassifLayer skyAbove = map.addLayer("sky.top", Spec.of("celestial"));
        sky = new PeakFinderSky(map, skyBelow, skyAbove,
                                host.context().getResources().getDisplayMetrics().density);
        standOnGround();

        host.button("North", new Runnable() {
            @Override
            public void run() {
                turnTo(0);
            }
        });
        host.button("Look at", new Runnable() {
            @Override
            public void run() {
                if (selected != null) {
                    turnTo(-bearing(lat, lon, selected.position));
                }
            }
        });
        host.button("Fly to", new Runnable() {
            @Override
            public void run() {
                if (selected != null) {
                    flyTo(selected);
                }
            }
        });
        host.toggle("Hours", false, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                hours = on;
            }
        });
        host.slider("eye height, m", 0, 4000, (float) eye, new ExampleHost.OnValue() {
            @Override
            public void onValue(float metres) {
                eye = metres;
                terrain.set("focusLift", metres);
            }
        });
        host.slider("field of view", 5, 120, host.option("fov", 46), new ExampleHost.OnValue() {
            @Override
            public void onValue(float degrees) {
                setFov(degrees);
            }
        });
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                tick();
            }
        }, 0);
    }

    private void setFov(float degrees) {
        map.set("fieldOfViewY", degrees);
        // The ridge term's tap spacing: radians per screen pixel.
        map.options().call("terrainOptions.setSurfaceParameter", "uPixelAngle",
                           Math.toRadians(degrees) / Math.max(map.view().getHeight(), 1)).close();
    }

    /** The eye stands focusLift over the ground under it, which the renderer keeps every frame. */
    private void placeCamera() {
        view.call("moveCameraTo", new Position(lon, lat), 13, rotation, tilt).close();
        terrain.set("focusLift", eye);
    }

    private void turnTo(double degrees) {
        rotation = degrees;
        tilt = map.camera().tilt();
        placeCamera();
    }

    /** Standing on the summit, facing the way it was seen from. */
    private void flyTo(Peak peak) {
        rotation = -bearing(lat, lon, peak.position);
        lat = peak.position.lat;
        lon = peak.position.lng;
        select(null);
        placeCamera();
        standOnGround();
    }

    /**
     * Placed again once the ground is known: an eye placed before the viewpoint's elevation arrived
     * stays where it was put until the camera next moves.
     */
    private void standOnGround() {
        withGround(new Runnable() {
            @Override
            public void run() {
                placeCamera();
                rebuildPeaks();
            }
        });
    }

    /**
     * A new style per viewpoint, since the eye's altitude is baked into the names' rank; the
     * selected summit is a style parameter, set on the live one.
     */
    private void rebuildPeaks() {
        generation++;
        String styleId = "peaks.style." + generation;
        MassifObject style = map.style(styleId, Spec.of("mbvt")
            .set("cartocss", Spec.of("cartocss")
                .set("css", String.format(Locale.ROOT, "@eye_elevation: %d;\n@label_band: %.3f;\n%s",
                                          Math.round(ground + eye), labelBand(), summitsCss))));
        style.set("params.selected_peak", selectedKey());
        MassifLayer layer = map.addLayer("peaks.layer." + generation, Spec.of("vector")
            .set("source", "peaks")
            .set("style", styleId)
            .set("preloading", true)
            .set("labelRenderOrder", "VECTOR_TILE_RENDER_ORDER_LAST")
            .set("tileSubstitutionPolicy", "TILE_SUBSTITUTION_POLICY_VISIBLE"));
        layer.moveTo(map.layerCount() - 2);
        layer.onFeatureClick(new MapEvents.Handler<MapEvents.VectorTileClick>() {
            @Override
            public void handle(MapEvents.VectorTileClick e) {
                String name = e.property("name");
                // The key is `[name] + '|' + [ele]` as the style writes it: a whole number without a decimal.
                double ele = e.propertyDouble("ele", Double.NaN);
                if (name != null) {
                    select(new Peak(name, Double.isNaN(ele) ? null
                        : ele == Math.rint(ele) ? Long.toString((long) ele) : Double.toString(ele), e.position()));
                }
            }
        });
        if (generation > 1) {
            map.removeLayer("peaks.layer." + (generation - 1));
            Massif.destroy("style", "peaks.style." + (generation - 1));
        }
    }

    /**
     * The row the names hang from, low enough for a name wrapped at 70 px, 13 px text at 45 degrees,
     * to fit under the bar - the Alpimaps app's row (look.mjs labelBand).
     */
    private double labelBand() {
        float density = host.context().getResources().getDisplayMetrics().density;
        double width = 70 + 13 * 2.5 + 10;
        double row = width * Math.sin(Math.PI / 4) + (13 * 2.6 + 4) * Math.cos(Math.PI / 4) + 10;
        double height = map.view().getHeight();
        return height > 0 ? Math.min(0.9, (host.topInset() + row * density) / height) : 0.2;
    }

    private String selectedKey() {
        return selected == null ? "" : selected.name + "|" + (selected.ele == null ? "" : selected.ele);
    }

    private void select(Peak peak) {
        selected = peak;
        MassifObject style = Massif.style("peaks.style." + generation);
        if (style != null) {
            style.set("params.selected_peak", selectedKey());
        }
        shown = "";
    }

    /** The heading, the rise and set, and the selected summit, refreshed as the view turns. */
    private void tick() {
        rotation = map.camera().rotation();
        tilt = map.camera().tilt();
        riseSet = sky.update(lat, lon, eye, hours);
        double heading = ((-rotation % 360) + 360) % 360;
        String text = String.format(Locale.ROOT, "%d° %s  ·  %s", Math.round(heading) % 360,
                                    PeakFinderSky.compass(heading), riseSet);
        if (selected != null) {
            double km = distance(lat, lon, selected.position) / 1000;
            double toPeak = (bearing(lat, lon, selected.position) + 360) % 360;
            text += String.format(Locale.ROOT, "\n%s  ·  %s%.1f km  ·  %d° %s", selected.name,
                                  selected.ele != null ? selected.ele + " m  ·  " : "",
                                  km, Math.round(toPeak), PeakFinderSky.compass(toPeak));
        } else {
            text += "\nDrag to look around, tap a summit name.";
        }
        if (!text.equals(shown)) {
            shown = text;
            host.caption(text);
        }
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                tick();
            }
        }, 200);
    }

    /** The ground under the viewpoint, off the DEM tile itself: the names rank by the eye's altitude. */
    private void withGround(final Runnable then) {
        final int zoom = 12;
        final double tiles = 1 << zoom;
        final double x = (lon + 180) / 360 * tiles;
        double phi = Math.toRadians(lat);
        final double y = (1 - Math.log(Math.tan(phi) + 1 / Math.cos(phi)) / Math.PI) / 2 * tiles;
        dem.loadTileAsync((int) x, (int) y, zoom, new MassifSource.TileCallback() {
            @Override
            public void onTile(byte[] data) {
                Bitmap bitmap = data != null ? BitmapFactory.decodeByteArray(data, 0, data.length) : null;
                if (bitmap != null) {
                    int pixel = bitmap.getPixel((int) ((x % 1) * bitmap.getWidth()),
                                                (int) ((y % 1) * bitmap.getHeight()));
                    ground = ((pixel >> 16) & 0xff) * 256 + ((pixel >> 8) & 0xff)
                             + (pixel & 0xff) / 256.0 - 32768;
                }
                then.run();
            }
        });
    }

    private String asset(String name) {
        try (InputStream in = host.context().getAssets().open("peak-finder/" + name)) {
            return new Scanner(in, "UTF-8").useDelimiter("\\A").next();
        } catch (IOException e) {
            throw new IllegalStateException("peak-finder/" + name, e);
        }
    }

    static double bearing(double lat, double lon, Position to) {
        double phi1 = Math.toRadians(lat);
        double phi2 = Math.toRadians(to.lat);
        double dLon = Math.toRadians(to.lng - lon);
        double y = Math.sin(dLon) * Math.cos(phi2);
        double x = Math.cos(phi1) * Math.sin(phi2) - Math.sin(phi1) * Math.cos(phi2) * Math.cos(dLon);
        return Math.toDegrees(Math.atan2(y, x));
    }

    static double distance(double lat, double lon, Position to) {
        double dLat = Math.toRadians(to.lat - lat);
        double dLon = Math.toRadians(to.lng - lon);
        double a = Math.pow(Math.sin(dLat / 2), 2)
                   + Math.cos(Math.toRadians(lat)) * Math.cos(Math.toRadians(to.lat)) * Math.pow(Math.sin(dLon / 2), 2);
        return 2 * 6371008.8 * Math.asin(Math.sqrt(a));
    }
}
