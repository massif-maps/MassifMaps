package com.massifmaps.MassifDemo.demo;

import android.content.Context;
import android.util.Log;

import com.massifmaps.core.BinaryData;
import com.massifmaps.styles.CartoCSSStyleSet;
import com.massifmaps.styles.CompiledStyleSet;
import com.massifmaps.utils.AndroidAssetPackage;
import com.massifmaps.utils.AssetPackage;
import com.massifmaps.utils.DirAssetPackage;
import com.massifmaps.utils.ZippedAssetPackage;
import com.massifmaps.vectortiles.MBVectorTileDecoder;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;

/**
 * Everything style-related: builds the tile decoder for each {@link DemoConfig.StyleSource}.
 *
 * The four sources answer four different questions:
 *  - DIR    : "does my style folder render?" - DirAssetPackage reads the style straight from a
 *             FOLDER on the device, so editing a .mss and restarting the app is the whole loop;
 *  - ZIP    : the same style, packaged (what production ships);
 *  - INLINE : a self-contained CartoCSS string - no file on the device, always works, and is the
 *             style that documents the composite slot syntax ('#hillshade', '#satellite', ...);
 *  - PROJECT: an in-memory style PROJECT (project.json + style.mss zipped). Only a project can
 *             declare 'param::' parameters, which is how a user setting drives the style at runtime;
 *  - ASSETS : the style PROJECT bundled in the APK (app/src/main/assets/style), read with
 *             AndroidAssetPackage. APK assets are not files, so DirAssetPackage cannot read them.
 *             This is also the reference example of a style that composite slots work with.
 */
public final class DemoStyles {

    private static final String TAG = "DemoStyles";

    /** Set by {@link #create} to whatever was really loaded, for the panel/log to show. */
    public static String lastLoadedDescription = "";

    /**
     * Builds a decoder for the given style source, falling back down the list (dir -> zip ->
     * inline) rather than crashing: on a device without the data files the demo must still start.
     */
    public static MBVectorTileDecoder create(Context context, DemoConfig.StyleSource source,
                                             String dataPath) {
        switch (source) {
            case DIR: {
                AssetPackage pack = openDir(dataPath);
                if (pack == null) {
                    pack = openZip(dataPath);
                }
                if (pack != null) {
                    return new MBVectorTileDecoder(styleSet(pack));
                }
                break;
            }
            case ZIP: {
                AssetPackage pack = openZip(dataPath);
                if (pack != null) {
                    return new MBVectorTileDecoder(styleSet(pack));
                }
                break;
            }
            case ASSETS: {
                AssetPackage pack = openAppAssets();
                if (pack != null) {
                    return new MBVectorTileDecoder(new CompiledStyleSet(pack, DemoConfig.STYLE_ASSETS_NAME));
                }
                break;
            }
            case ASSETZIP: {
                AssetPackage pack = openAssetZip(context, DemoConfig.STYLE_ASSET_ZIP_NAME);
                if (pack != null) {
                    return new MBVectorTileDecoder(styleSet(pack));
                }
                break;
            }
            case POI: {
                // The CartoCSS is written here, the FONTS come from the APK asset package: a shield
                // icon shaped from osm.ttf needs a font, and a bare CartoCSS string carries none.
                AssetPackage pack = openAppAssets();
                if (pack != null) {
                    lastLoadedDescription = "shield test style + app asset fonts";
                    return new MBVectorTileDecoder(new CartoCSSStyleSet(poiTestStyle(), pack));
                }
                break;
            }
            case PROJECT: {
                MBVectorTileDecoder decoder = createProjectDecoder();
                if (decoder != null) {
                    return decoder;
                }
                break;
            }
            case INLINE:
            default:
                break;
        }
        lastLoadedDescription = "inline CartoCSS";
        return new MBVectorTileDecoder(new CartoCSSStyleSet(inlineStyle()));
    }

    /**
     * The project to compile out of a package that holds several. A converted Mapbox Standard
     * writes one per light preset over a shared style.mss, so --es lightPreset night picks the
     * dark one. Falls back to whichever project CompiledStyleSet finds first, which is what every
     * single-project style wants.
     */
    private static CompiledStyleSet styleSet(AssetPackage pack) {
        String preset = DemoConfig.LIGHT_PRESET;
        if (preset != null && !preset.isEmpty()) {
            try {
                return new CompiledStyleSet(pack, preset);
            } catch (Exception e) {
                Log.w(TAG, "no style project '" + preset + "' in this package: " + e.getMessage());
            }
        }
        return new CompiledStyleSet(pack);
    }

    /**
     * The live-editable style: a plain folder on the device, read through the SDK's
     * DirAssetPackage. Push a modified style with:
     *   adb push my-style/. /sdcard/alpimaps_mbtiles/osm/
     * and restart the app - no repackaging, no rebuild.
     */
    private static AssetPackage openDir(String dataPath) {
        String dirPath = dataPath + "/" + DemoConfig.STYLE_DIR_NAME;
        try {
            DirAssetPackage pack = new DirAssetPackage(dirPath);
            lastLoadedDescription = "dir " + dirPath + " (" + pack.getAssetNames().size() + " assets)";
            Log.i(TAG, "style: " + lastLoadedDescription);
            return pack;
        } catch (Exception e) {
            // Missing folder is the normal case on a device where only osm.zip was pushed.
            Log.w(TAG, "style dir not usable (" + dirPath + "): " + e.getMessage());
            return null;
        }
    }

    /**
     * The style bundled in the APK. Note the asset manager must already be connected, which a
     * MapView does when it is constructed - so this cannot be called before the map view exists.
     */
    private static AssetPackage openAppAssets() {
        try {
            AndroidAssetPackage pack = new AndroidAssetPackage(DemoConfig.STYLE_ASSETS_PATH);
            lastLoadedDescription = "app assets " + DemoConfig.STYLE_ASSETS_PATH
                    + " (" + pack.getAssetNames().size() + " assets)";
            Log.i(TAG, "style: " + lastLoadedDescription);
            return pack;
        } catch (Exception e) {
            Log.w(TAG, "app asset style not usable (" + DemoConfig.STYLE_ASSETS_PATH + "): " + e.getMessage());
            return null;
        }
    }

    /**
     * A style project zip bundled in the APK (assets/styles/<name>.zip, built by gradle's
     * zipStyleProjects). AndroidAssetPackage cannot be used: it expects a FOLDER of assets, and
     * these are single zip entries - the same ones the gallery examples reach through the facade
     * as 'assets://styles/<name>.zip'.
     */
    private static AssetPackage openAssetZip(Context context, String name) {
        String assetPath = "styles/" + name + ".zip";
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            try (InputStream in = context.getAssets().open(assetPath)) {
                byte[] chunk = new byte[16384];
                int read;
                while ((read = in.read(chunk)) > 0) {
                    out.write(chunk, 0, read);
                }
            }
            lastLoadedDescription = "asset zip " + assetPath;
            Log.i(TAG, "style: " + lastLoadedDescription);
            return new ZippedAssetPackage(new BinaryData(out.toByteArray()));
        } catch (Exception e) {
            Log.w(TAG, "asset style zip not usable (" + assetPath + "): " + e.getMessage());
            return null;
        }
    }

    private static AssetPackage openZip(String dataPath) {
        String zipPath = dataPath + "/" + DemoConfig.STYLE_ZIP_NAME;
        try {
            lastLoadedDescription = "zip " + zipPath;
            Log.i(TAG, "style: " + lastLoadedDescription);
            return new ZippedAssetPackage(readFile(zipPath));
        } catch (Exception e) {
            Log.w(TAG, "style zip not usable (" + zipPath + "): " + e.getMessage());
            return null;
        }
    }

    /** Style zip of the offline routes layer. */
    public static MBVectorTileDecoder createZipDecoder(String zipPath) {
        try {
            return new MBVectorTileDecoder(new CompiledStyleSet(new ZippedAssetPackage(readFile(zipPath))));
        } catch (Exception e) {
            Log.w(TAG, "could not read " + zipPath + ": " + e.getMessage());
            return null;
        }
    }

    public static BinaryData readFile(String path) throws java.io.IOException {
        File file = new File(path);
        FileInputStream stream = new FileInputStream(file);
        try {
            DataInputStream dataInputStream = new DataInputStream(stream);
            byte[] bytes = new byte[(int) file.length()];
            dataInputStream.readFully(bytes);
            return new BinaryData(bytes);
        } finally {
            stream.close();
        }
    }

    // =============================================================================================
    // INLINE STYLE
    // OpenMapTiles schema (openfreemap / akylas tiles). Text is omitted on purpose: labels need a
    // font asset package, which a raw CartoCSS string cannot provide.
    //
    // COMPOSITE SLOTS: in a CompositeVectorTileLayer the position of a source in the draw order is
    // the position of the FIRST rule referencing its '#name'; the '#name { ... }' block is where
    // the per-source settings live (they accept zoom-dependent expressions).
    // =============================================================================================

    /** 'polygon-opacity' for the ground-shaped fills, or nothing at all while they are opaque -
     *  so the default style string is byte-identical to what it was before the knob existed. */
    private static String landcoverOpacity() {
        if (DemoConfig.INLINE_LANDCOVER_OPACITY >= 1.0f) {
            return "";
        }
        return " polygon-opacity: " + DemoConfig.INLINE_LANDCOVER_OPACITY + ";";
    }

    /** A LAYER-level 'comp-op', which is the one that makes the renderer composite the layer
     *  through its overlay buffer (and re-stamp the stencil tile masks into it) instead of
     *  drawing it straight into the frame. Symbolizer-level properties such as
     *  'polygon-comp-op' do NOT take that path, so this is what exercises it. */
    private static String compOp() {
        if (DemoConfig.INLINE_COMP_OP == null || DemoConfig.INLINE_COMP_OP.isEmpty()) {
            return "";
        }
        return " comp-op: " + DemoConfig.INLINE_COMP_OP + ";";
    }

    /** An ARGB int as the '#rrggbb' CartoCSS literal (the alpha goes in a *-opacity property). */
    private static String hex(int argb) {
        return String.format("#%06X", argb & 0xFFFFFF);
    }

    public static String inlineStyle() {
        StringBuilder map = new StringBuilder("Map { background-color: ").append(DemoConfig.INLINE_BACKGROUND_COLOR).append(";");
        if (DemoConfig.INLINE_STYLE_LIGHTING) {
            // The same sun/shadow/fog values the code sets on LightOptions/TerrainOptions, but
            // expressed IN the style - and zoom-dependent, which only the style can do.
            map.append(" terrain-lighting: 1;")
               .append(" sun-azimuth: 250;")
               .append(" sun-altitude: linear([view::zoom], (11, 55), (15, 12));")
               .append(" sun-intensity: 1;")
               .append(" ambient-intensity: 0.4;")
               .append(" shadow-strength: 0.8;")
               .append(" shadow-softness: 1;")
               .append(" terrain-max-visible-distance: 40000;");
        }
        // Outside the styleLight gate on purpose: every one of these defaults to the engine's own
        // value, so emitting them always is a no-op until the matching --es knob sets one. Inside
        // the gate they were unreachable without --es styleLight true, which is how bldAmbient and
        // bldGradient both looked broken.
        map.append(" building-light-intensity: " + DemoConfig.INLINE_BUILDING_LIGHT + ";")
           .append(" building-ambient: " + DemoConfig.INLINE_BUILDING_AMBIENT + ";")
           .append(" building-vertical-gradient: " + DemoConfig.INLINE_BUILDING_GRADIENT + ";")
           .append(" building-vertical-gradient-height: " + DemoConfig.INLINE_BUILDING_GRADIENT_HEIGHT + ";")
           .append(" building-ao-ground-radius: " + DemoConfig.INLINE_BUILDING_AO_RADIUS + ";")
           .append(" building-ao-intensity: " + DemoConfig.INLINE_BUILDING_AO_INTENSITY + ";")
           .append(" building-ao-ground-step: " + DemoConfig.INLINE_BUILDING_AO_STEP + ";")
           .append(" building-ao-ground-attenuation: " + DemoConfig.INLINE_BUILDING_AO_ATTENUATION + ";")
           .append(" building-edge-radius: " + DemoConfig.INLINE_BUILDING_EDGE_RADIUS + ";")
           .append(" building-roof-shade: " + DemoConfig.INLINE_BUILDING_ROOF_SHADE + ";")
           .append(" building-rounded-roof: " + DemoConfig.INLINE_BUILDING_ROUNDED_ROOF + ";");
        if (DemoConfig.FOG_SOURCE_STYLE.equals(DemoConfig.FOG_SOURCE)) {
            // The mapbox 'fog' property set, written in the style. The range is in multiples of the
            // camera-to-focus distance, so it needs no per-zoom expression - fog-range-end is one
            // here only to show that the style CAN make it zoom-dependent and the options cannot.
            map.append(" fog-color: #dc9f9f;")
               .append(" fog-range-start: 0.8;")
               .append(" fog-range-end: linear([view::zoom], (11, 8), (15, 4));")
               .append(" fog-high-color: #245bde;")
               .append(" fog-space-color: #000000;")
               .append(" fog-horizon-blend: 0.5;")
               .append(" fog-star-intensity: 0.15;");
        }
        map.append(" }");

        if (DemoConfig.INLINE_STYLE_MINIMAL) {
            // Background plus the composite slots only: no vector geometry, so a frame costs the
            // terrain and the slots and nothing else. The slot blocks have to stay - a source's
            // position in the draw order IS the position of the first rule naming it.
            return String.join("\n",
                map.toString(),
                "#hillshade[zoom>=4][zoom<=19] {",
                "  hillshade-illumination-direction: " + (int) DemoConfig.INLINE_HILLSHADE_ILLUMINATION + ";",
                "  hillshade-shadow-color: " + DemoConfig.INLINE_HILLSHADE_SHADOW_COLOR + ";",
                "}",
                "#satellite[zoom>=" + DemoConfig.INLINE_SATELLITE_MIN_ZOOM + "] { raster-opacity: 1; raster-comp-op: src-over; }");
        }

        return String.join("\n",
            map.toString(),
            "#water { polygon-fill: #9cc3e0; }",
            // Ground-shaped fills carry the landcover opacity: opaque by default, translucent when
            // the hillshade and the contours underneath have to read through them (tangram's
            // 'translucent-polygons', alpha 0.25).
            "#landuse { polygon-fill: #dddddd;" + landcoverOpacity() + " }",
            "#landcover { polygon-fill: #dbe8cc;" + landcoverOpacity() + compOp() + " }",
            // --- composite slots, in draw order ---
            "#satellite[zoom>=" + DemoConfig.INLINE_SATELLITE_MIN_ZOOM + "] { raster-opacity: 1; raster-comp-op: src-over; }",
            "#hillshade[zoom>=4][zoom<=19] {",
            "  hillshade-illumination-direction: " + (int) DemoConfig.INLINE_HILLSHADE_ILLUMINATION + ";",
            "  hillshade-shadow-color: " + DemoConfig.INLINE_HILLSHADE_SHADOW_COLOR + ";",
            // The composite slot takes its contour settings from the STYLE, not from the
            // HillshadeRasterTileLayer setters (those only reach the stand-alone layer) - so this
            // is what turns the shader-drawn contour lines on in the composite base.
            DemoConfig.HILLSHADE_CONTOUR_LINES
                ? String.join("\n",
                    "  hillshade-contour-interval: " + (int) DemoConfig.HILLSHADE_CONTOUR_INTERVAL + ";",
                    "  hillshade-contour-width: " + DemoConfig.HILLSHADE_CONTOUR_WIDTH + ";",
                    "  hillshade-contour-color: " + DemoStyles.hex(DemoConfig.HILLSHADE_CONTOUR_COLOR_ARGB) + ";")
                : "",
            "}",

                "#contour[zoom>=" + DemoConfig.CONTOUR_MIN_VISIBLE_ZOOM + "] {",
                // Lines only for the traced geometry: a label stub is a ~20 point fragment of a
                // contour, long enough to lay text along and nothing more, so drawing it as a line
                // paints dashes over the map. Both modes carry 'stub', so the filter is safe in
                // either. In stub mode the LINES come from the hillshade shader instead.
                "  [stub=0] {",
                "    line-color: #C56008;",
                contourWidthByDiv(),
                "  }",
                DemoConfig.INLINE_LABELS
                        ? String.join("\n",
                        "[div=1000][zoom>=12],",
                        "[div=500][zoom>=12],",
                        "[div=200][zoom>=14],",
                        "[div=250][zoom>=13][zoom<14],",
                        "[div=100][zoom>=14],",
                        "[div=50][zoom>=15] {",
                        "text-name: [ele]+' m';",
                        "text-fill: #000000;",
                        " text-spacing: 10;",
                        "text-placement: line;",
                        // [zoom] here is the CONTOUR TILE zoom, which never drops below the DEM zoom - it
                        // does not gate on the camera. [view::zoom] is evaluated per frame, and size 0
                        // hides the label.
                        "text-size: linear([view::zoom], (11.99, 0), (12, 14));",
                        "}")
                        : "",
                "  contour-base-interval: " + (int) DemoConfig.CONTOUR_BASE_INTERVAL + ";",
                // The composite slot reads the source's generation parameters from the style too.
                DemoConfig.CONTOUR_LABEL_STUBS
                        ? String.join("\n",
                        "  contour-label-stubs: 1;",
                        "  contour-label-interval: " + (int) DemoConfig.CONTOUR_LABEL_INTERVAL + ";")
                        : "",
                "}",
            "#transportation { line-color: #ffffff; line-width: " + DemoConfig.INLINE_ROAD_WIDTH + "; }",
            DemoConfig.INLINE_LABELS
                ? String.join("\n",
                    "#transportation_name {",
                        "text-name: [name];",
                        "text-fill: #000000;",
                        " text-spacing: 10;",
                        "text-placement: [render::3d] ?billboard-line:line;",
                        "text-size: 10;",
                        DemoConfig.LABEL_MAX_DISTANCE > 0
                            ? "text-max-distance: " + DemoConfig.LABEL_MAX_DISTANCE + ";"
                            : "",
                        DemoConfig.INLINE_TEXT_OCCLUSION_OPACITY >= 0
                            ? "text-occlusion-opacity: " + DemoConfig.INLINE_TEXT_OCCLUSION_OPACITY + ";"
                            : "",
                        " }")
                : "",
            "#transportation['class'='motorway'] { line-color: #e27d60; line-width: " + DemoConfig.INLINE_MOTORWAY_WIDTH + "; }",
            DemoConfig.INLINE_BUILDINGS_3D
                ? "#building[zoom>=14] { building-fill: " + DemoConfig.INLINE_BUILDING_COLOR + "; building-height: [render_height] ? [render_height] : 5;\n" +
                  "      building-min-height: [render_min_height]; building-roof-shape: 'gabled'; building-roof-height: 4; }"
                : "#building[zoom>=14] { polygon-fill: " + DemoConfig.INLINE_BUILDING_COLOR + "; }");
    }

    // =============================================================================================
    // SHIELD TEST STYLE (StyleSource.POI)
    // One shield rule per label: an ICON that stays on the feature and a NAME the culler puts on
    // whichever side is free ('shield-anchors'), falling back to the icon alone when none is
    // ('shield-text-optional'). The icon is a GLYPH of assets/style/fonts/osm.ttf - the same font
    // the real style uses - so it costs one atlas cell and no bitmap.
    //
    // Deliberately dense: every '#poi' and every '#place' carries one, which is what makes the
    // side selection visible (and what a perf comparison needs).
    // =============================================================================================

    /** A PUA glyph of assets/style/fonts/osm.ttf, as the real style's 'param::osm-*' values have them. */
    private static final String ICON_DOT = "\ue934";
    private static final String ICON_PEAK = "\uea04";
    private static final String ICON_RESTAURANT = "\ue919";
    private static final String ICON_HOTEL = "\ue9d6";
    private static final String ICON_CAFE = "\ue990";

    /** The shield properties shared by every rule of the test style. */
    private static String shieldCommon(String icon, String fill, float size) {
        StringBuilder mss = new StringBuilder();
        mss.append("  shield-face-name: 'DIN Pro Medium';\n");
        mss.append("  shield-size: ").append(size).append(";\n");
        mss.append("  shield-fill: ").append(fill).append(";\n");
        mss.append("  shield-halo-fill: #ffffff;\n");
        mss.append("  shield-halo-radius: 1.5;\n");
        mss.append("  shield-text-dx: ").append(DemoConfig.POI_TEXT_DX).append(";\n");
        mss.append("  shield-wrap-width: ").append(DemoConfig.POI_WRAP_WIDTH).append(";\n");
        mss.append("  shield-wrap-character: ' ';\n");
        if (DemoConfig.POI_BITMAP_ICON) {
            mss.append("  shield-file: url(shields/place.svg);\n");
        }
        if (DemoConfig.POI_FONT_ICON) {
            mss.append("  shield-icon-name: '").append(icon).append("';\n");
            mss.append("  shield-icon-face-name: 'osm';\n");
            mss.append("  shield-icon-size: ").append(size + 4f).append(";\n");
            mss.append("  shield-icon-fill: ").append(fill).append(";\n");
        }
        if (DemoConfig.POI_TEXT_ALIGN != null && !DemoConfig.POI_TEXT_ALIGN.trim().isEmpty()) {
            mss.append("  shield-text-horizontal-alignment: '").append(DemoConfig.POI_TEXT_ALIGN.trim()).append("';\n");
        }
        if (DemoConfig.POI_TEXT_BG) {
            mss.append("  shield-background-fill: #ffffff;\n");
            mss.append("  shield-background-opacity: 0.85;\n");
            mss.append("  shield-background-radius: ").append(DemoConfig.POI_BG_RADIUS).append(";\n");
            mss.append("  shield-background-padding-x: ").append(DemoConfig.POI_BG_PADDING).append(";\n");
            mss.append("  shield-background-padding-y: ").append(DemoConfig.POI_BG_PADDING * 0.6f).append(";\n");
            if (DemoConfig.POI_BG_BORDER > 0) {
                mss.append("  shield-background-border-fill: ").append(fill).append(";\n");
                mss.append("  shield-background-border-width: ").append(DemoConfig.POI_BG_BORDER).append(";\n");
            }
        }
        if (DemoConfig.POI_ICON_BG) {
            mss.append("  shield-icon-background-fill: #ffffff;\n");
            mss.append("  shield-icon-background-opacity: 0.9;\n");
            mss.append("  shield-icon-background-radius: 20;\n");   // a pill around the icon
            mss.append("  shield-icon-background-padding-x: ").append(DemoConfig.POI_BG_PADDING).append(";\n");
            mss.append("  shield-icon-background-padding-y: ").append(DemoConfig.POI_BG_PADDING).append(";\n");
            if (DemoConfig.POI_BG_BORDER > 0) {
                mss.append("  shield-icon-background-border-fill: ").append(fill).append(";\n");
                mss.append("  shield-icon-background-border-width: ").append(DemoConfig.POI_BG_BORDER).append(";\n");
            }
        }
        if (DemoConfig.POI_ANCHORS != null && !DemoConfig.POI_ANCHORS.trim().isEmpty()) {
            mss.append("  shield-anchors: '").append(DemoConfig.POI_ANCHORS.trim()).append("';\n");
            mss.append("  shield-text-optional: ").append(DemoConfig.POI_TEXT_OPTIONAL ? "true" : "false").append(";\n");
        }
        return mss.toString();
    }

    public static String poiTestStyle() {
        String css = String.join("\n",
            "Map { background-color: #f4f1ec; }",
            "#water { polygon-fill: #9cc3e0; }",
            "#landcover { polygon-fill: #dbe8cc; }",
            "#landuse { polygon-fill: #e7e3dc; }",
            "#transportation { line-color: #ffffff; line-width: linear([view::zoom], (12, 0.6), (18, 4.0)); }",
            "#transportation['class'='motorway'] { line-color: #e8b48a; line-width: linear([view::zoom], (12, 1.5), (18, 9.0)); }",
            "#building[zoom>=15] { polygon-fill: #ded8d0; }",

            // Cities and towns: the low-zoom test - a screen full of them, all competing.
            "#place[class=city][zoom>=4],",
            "#place[class=town][zoom>=8],",
            "#place[class=village][zoom>=11] {",
            "  shield-name: [name];",
            shieldCommon(ICON_DOT, "#333333", 12f),
            "  shield-placement-priority: 10;",
            "}",

            // Every POI, at the zooms where a real style shows them. One rule per class rather than
            // nested filter blocks: a nested block builds a symbolizer of its own, and what it
            // inherits from the block around it is a CartoCSS question this test has no reason to
            // ask. Several icons so the atlas holds more than one glyph and the screen mixes label
            // widths, which is what makes the side selection visible.
            "#poi[zoom>=14][class=restaurant],",
            "#poi[zoom>=14][class=fast_food] {",
            "  shield-name: [name];",
            shieldCommon(ICON_RESTAURANT, "#b5651d", 11f),
            "}",
            "#poi[zoom>=14][class=lodging] {",
            "  shield-name: [name];",
            shieldCommon(ICON_HOTEL, "#2a6f97", 11f),
            "}",
            "#poi[zoom>=14][class=cafe] {",
            "  shield-name: [name];",
            shieldCommon(ICON_CAFE, "#7d5a3c", 11f),
            "}",
            "#poi[zoom>=14][class!=restaurant][class!=fast_food][class!=lodging][class!=cafe] {",
            "  shield-name: [name];",
            shieldCommon(ICON_CAFE, "#4a4a4a", 11f),
            "}",

            // Peaks: the 3D test - these sit on the terrain, so their icons ride the relief.
            "#mountain_peak[zoom>=11] {",
            "  shield-name: [name];",
            shieldCommon(ICON_PEAK, "#5a4632", 11f),
            "}");
        return css;
    }

    /**
     * Style of the STAND-ALONE contour layer (DemoConfig.LAYER_CONTOUR).
     * ContourTileDataSource exposes 'ele' (metres) and 'div' (importance = largest nice divisor),
     * so the whole look is CartoCSS. NOTE: in CartoCSS 'zoom' is the TILE zoom, not the camera
     * zoom - contour tiles are generated at the DEM source zoom.
     */
    public static String contourStyle() {
        return String.join("\n",
            "#contour {",
            "  line-color: #C56008;",
            contourWidthByDiv(),
            // Labels need a font asset package (text-face-name -> a bundled font), so they are
            // only available with a DIR/ZIP style, not with this raw CartoCSS string.
            "}");
    }

    /**
     * Which contours are VISIBLE, per camera zoom. The tile carries every line its zoom can place
     * (see ContourTileDataSource::getIntervalForZoom), and 'div' - the largest nice divisor of the
     * elevation - ranks them; the style fades a rank in when the camera is close enough for it.
     *
     * This has to be a WIDTH ramp, not a filter: a CartoCSS filter is evaluated per tile at decode
     * time, so it cannot see the camera. [view::zoom] is evaluated per frame, and a width of 0
     * draws nothing (the quad is degenerate).
     */
    private static String contourWidthByDiv() {
        return String.join("\n",
            "  line-opacity: 0.75;",
            "  line-width: 0;",
            "  [div>=10]  { line-width: linear([view::zoom], (14, 0), (14.5, 0.5));  line-opacity: linear([view::zoom], (14, 0), (14.5, 1)); }",
            "  [div>=50]  { line-width: linear([view::zoom], (13, 0), (13.5, 0.7)); line-opacity: linear([view::zoom], (13, 0), (13.5, 1));}",
            "  [div>=100] { line-width: linear([view::zoom], (11.5, 0), (12, 1)); line-opacity: 0.9; }",
            "  [div>=500] { line-width: 1.3; line-opacity: 0.9; }");
    }

    /**
     * Style of the PRE-BAKED contour tile layer (DemoConfig.LAYER_CONTOUR_TILES).
     *
     * This is the '#contour' block of assets/style/shared/terrain.less, verbatim except that the
     * style variables are inlined with their osm/style.less + shared/style.less values and the
     * ['param::contours'>0] guard is dropped (a raw CartoCSS string can not declare style
     * parameters). Same rules, same 'ele'/'div' attributes as the generated contours, so the two
     * layers can be compared one against the other.
     *
     * NOTE: 'zoom' is the TILE zoom. The tileset stops at z14, so the [zoom>=15] label rule of the
     * original never fires here (kept as-is on purpose - it does not in the real style either
     * until the source goes deeper).
     */
    public static String contourTilesStyle() {
        String font = DemoConfig.CONTOUR_TILES_FONT;
        return String.join("\n",
            "#contour {",
            "  [div=10][zoom>=14],",
            "  [div=20][zoom>=14] {",
            "    line-color: #226600;",
            "    line-opacity: 0.2;",                                              // @contour_opacity * 0.5
            "    line-width: linear([view::zoom], (16, 0.6), (22, 1.6));",
            "  }",
            "",
            "  [div=100][zoom>=12],",
            "  [div=200][zoom>=12],",
            "  [div=50][zoom>=13] {",
            "    line-color: #226600;",
            "    line-opacity: step([view::zoom], (12, 0.2), (14, 0.4));",         // @contour_opacity_semi
            "    line-width: linear([view::zoom], (16, 0.6), (22, 1.6));",
            "  }",
            "",
            "  [div=1000][zoom>=12],",
            "  [div=500][zoom>=12],",
            "  [div=250][zoom>=13][zoom<14] {",
            "    line-color: #226600;",
            "    line-opacity: 0.4;",                                              // [param::contoursOpacity]
            "    line-width: linear([view::zoom], (16, 0.6), (22, 1.6));",
            "  }",
            "",
            "  [div=1000][zoom>=12],",
            "  [div=500][zoom>=12],",
            "  [div=200][zoom>=14],",
            "  [div=250][zoom>=13][zoom<14],",
            "  [div=100][zoom>=14],",
            "  [div=50][zoom>=15] {",
            "    text-face-name: '" + font + "';",
            "    text-name: [ele]+' m';",
            "    text-fill: #226600;",
            "    text-spacing: 10;",
            "    text-placement: line;",
            "    text-halo-radius: 1;",
            "    text-halo-fill: #f2f5f888;",
            "    text-size: linear([view::zoom], (12, 7), (16, 8), (20, 9));",
            "  }",
            "}");
    }

    // =============================================================================================
    // STYLE PARAMETER STYLE
    // 'param::' parameters are user settings the style reacts to at runtime
    // (decoder.setStyleParameter). They can only be DECLARED in a style project, so the project is
    // built in memory here: project.json + style.mss, zipped, wrapped in a CompiledStyleSet.
    // =============================================================================================

    /** Name of the boolean parameter the demo flips; see DemoMap.startParamToggleLoop. */
    public static final String BOOL_PARAMETER = "show_relief";

    /**
     * A parameter that nothing but a colour reads. The SDK classifies it as LIVE: setting it swaps
     * the value the decoded tiles already point at and asks for a redraw, where BOOL_PARAMETER sits
     * in a filter and so decides what the tile contains - changing that one decodes every tile
     * again. Flipped by the same loop, so a run shows both paths.
     */
    public static final String COLOR_PARAMETER = "water_color";

    /**
     * A parameter holding a TABLE - one colour per road class - that the style reads with
     * get(table, key, fallback). One parameter replaces one-per-class, and the app owns the
     * contents. Reading it uses the feature's [class], so changing the table still re-decodes.
     */
    public static final String TABLE_PARAMETER = "road_colors";

    private static MBVectorTileDecoder createProjectDecoder() {
        // 'layers' is TOP -> BOTTOM (reversed into draw order) and must list every composite slot.
        String projectJson = String.join("\n",
            "{",
            "  \"styles\": [\"style.mss\"],",
            "  \"layers\": [\"contour\", \"building\", \"transportation\", \"satellite\", \"hillshade\", \"landcover\", \"water\"],",
            "  \"styleparameters\": {",
            "    \"" + BOOL_PARAMETER + "\": { \"default\": true },",
            "    \"" + COLOR_PARAMETER + "\": { \"default\": \"#9cc3e0\" },",
            "    \"" + TABLE_PARAMETER + "\": { \"default\": { \"motorway\": \"#e27d60\", \"trunk\": \"#f0a868\", \"primary\": \"#d9b382\" } }",
            "  }",
            "}");
        String mss = String.join("\n",
            "Map { background-color: " + DemoConfig.INLINE_BACKGROUND_COLOR + "; }",
            "#water { polygon-fill: [param::" + COLOR_PARAMETER + "]; }",
            "#landcover { polygon-fill: #dbe8cc;" + landcoverOpacity() + " }",
            // the hillshade slot exists only while the user setting is on
            "#hillshade['param::" + BOOL_PARAMETER + "'=true][zoom>=4] {",
            "  hillshade-opacity: linear([view::zoom], (4, 0.5), (12, 0.9));",
            "  hillshade-exaggeration: linear([view::zoom], (4, 0.6), (12, 1.4));",
            "  hillshade-illumination-direction: 315;",
            "  hillshade-shadow-color: #103040;",
            "}",
            "#satellite[zoom>=" + DemoConfig.INLINE_SATELLITE_MIN_ZOOM + "] { raster-opacity: 0.45; }",
                "#contour[zoom>=12] { line-color: #9a5a12; line-width: 0.8; line-opacity: 0.7; }",
            // one rule for every class, the colour comes out of the table parameter
            "#transportation { line-color: get([param::" + TABLE_PARAMETER + "], [class], #ffffff); line-width: 1.2; }",
            "#transportation['class'='motorway'] { line-width: " + DemoConfig.INLINE_MOTORWAY_WIDTH + "; }",
            DemoConfig.INLINE_BUILDINGS_3D
                ? "#building[zoom>=14] { building-fill: " + DemoConfig.INLINE_BUILDING_COLOR + "; building-height: " + DemoConfig.INLINE_BUILDING_HEIGHT + "; }"
                : "#building[zoom>=14] { polygon-fill: " + DemoConfig.INLINE_BUILDING_COLOR + "; }");

        try {
            java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
            java.util.zip.ZipOutputStream zos = new java.util.zip.ZipOutputStream(bos);
            String[][] entries = new String[][] { { "project.json", projectJson }, { "style.mss", mss } };
            for (String[] entry : entries) {
                zos.putNextEntry(new java.util.zip.ZipEntry(entry[0]));
                zos.write(entry[1].getBytes("UTF-8"));
                zos.closeEntry();
            }
            zos.close();
            lastLoadedDescription = "in-memory style project";
            return new MBVectorTileDecoder(new CompiledStyleSet(new ZippedAssetPackage(new BinaryData(bos.toByteArray()))));
        } catch (Exception e) {
            Log.e(TAG, "could not build the style project", e);
            return null;
        }
    }

    /**
     * Decoder of the ROUTE SELECTION bench (DemoConfig.LAYER_ROUTE_SELECT). It needs a project,
     * not a bare CartoCSS string, because 'param::selected_id' has to be DECLARED before a rule can
     * read it - the same in-memory zip trick as {@link #createProjectDecoder()}.
     *
     * The style is written the way a real route style writes selection, so what the bench measures
     * is the real shape:
     *
     *     @is_selected: [param::selected_id] = [osmid] + '';
     *
     * In 'value' mode the parameter only reaches line-color and line-width - the appearance. In
     * 'filter' mode it also gates a casing attachment, which is what decides whether that geometry
     * exists at all; that is the half no repaint can answer.
     */
    public static MBVectorTileDecoder createRouteSelectDecoder() {
        String projectJson = String.join("\n",
            "{",
            "  \"styles\": [\"style.mss\"],",
            "  \"layers\": [\"routes\"],",
            "  \"styleparameters\": {",
            // 'selects' opts this parameter into the repaint path: the decoder folds the comparison
            // both ways so a selection change rewrites style bytes instead of decoding the tiles.
            // In 'filter' mode the same parameter also gates a rule, so it is refused (with a
            // warning saying why) and the bench measures the decode it always did.
            "    \"" + DemoConfig.ROUTE_SELECT_PARAMETER + "\": { \"default\": \"\", \"selects\": true }",
            "  }",
            "}");
        boolean filterMode = "filter".equalsIgnoreCase(DemoConfig.ROUTE_SELECT_MODE);
        StringBuilder mss = new StringBuilder();
        mss.append("@osm_id: [osmid] + '';\n");
        mss.append("@is_selected: [param::").append(DemoConfig.ROUTE_SELECT_PARAMETER).append("] = @osm_id;\n");
        mss.append("#routes {\n");
        if (filterMode) {
            // The structural half: the casing only EXISTS for the selected feature
            // spelled out, not @is_selected: a 'when' filter takes the comparison itself, which is
            // also how a real route style writes it
            mss.append("  when ([param::").append(DemoConfig.ROUTE_SELECT_PARAMETER).append("] = [osmid] + '') {\n")
               .append("    casing/line-color: #ffffff;\n")
               .append("    casing/line-width: ").append(DemoConfig.ROUTE_SELECT_WIDTH + DemoConfig.ROUTE_SELECT_WIDTH_BUMP + 4f).append(";\n")
               .append("    casing/line-join: round;\n")
               .append("    casing/line-cap: round;\n")
               .append("  }\n");
        }
        mss.append("  line-join: round;\n")
           .append("  line-cap: round;\n")
           .append("  line-color: @is_selected ? ").append(DemoConfig.ROUTE_SELECT_SELECTED_COLOR)
           .append(" : ").append(DemoConfig.ROUTE_SELECT_COLOR).append(";\n")
           .append("  line-width: ").append(DemoConfig.ROUTE_SELECT_WIDTH)
           .append(" + (@is_selected ? ").append(DemoConfig.ROUTE_SELECT_WIDTH_BUMP).append(" : 0);\n")
           .append("}");

        try {
            java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
            java.util.zip.ZipOutputStream zos = new java.util.zip.ZipOutputStream(bos);
            String[][] entries = new String[][] { { "project.json", projectJson }, { "style.mss", mss.toString() } };
            for (String[] entry : entries) {
                zos.putNextEntry(new java.util.zip.ZipEntry(entry[0]));
                zos.write(entry[1].getBytes("UTF-8"));
                zos.closeEntry();
            }
            zos.close();
            return new MBVectorTileDecoder(new CompiledStyleSet(new ZippedAssetPackage(new BinaryData(bos.toByteArray()))));
        } catch (Exception e) {
            Log.e(TAG, "could not build the route selection style", e);
            return null;
        }
    }

    /**
     * Style of the ROUTE TEST layer (DemoConfig.LAYER_ROUTE_TEST): a navigation route drawn the way
     * a turn-by-turn app draws it - a dark casing attachment first (CartoCSS renders attachments in
     * declaration order, so it lands UNDER) and the coloured fill over it.
     *
     * Both attachments carry the same join/cap/miterlimit, so one screenshot says what a setting
     * does to the whole route, casing included. line-opacity below 1 is the join over-blending
     * test: overlapping triangles of ONE line blend twice where they overlap.
     */
    public static String routeTestStyle() {
        // "layer" opacity is a layer-level property, so the renderer draws the whole layer opaque
        // into the overlay buffer and composites it once - overlaps can not blend twice, at the
        // cost of a full-screen pass per layer. "geom" bakes it into the colour, which is the path
        // the single-blend stencil pass covers.
        boolean layerOpacity = "layer".equalsIgnoreCase(DemoConfig.ROUTE_TEST_OPACITY_MODE);
        String common = " line-join: " + DemoConfig.ROUTE_TEST_JOIN
                + "; line-cap: " + DemoConfig.ROUTE_TEST_CAP
                + "; line-miterlimit: " + DemoConfig.ROUTE_TEST_MITER_LIMIT
                + (layerOpacity
                    ? "; opacity: " + DemoConfig.ROUTE_TEST_OPACITY + "; comp-op: src-over;"
                    : "; line-opacity: " + DemoConfig.ROUTE_TEST_OPACITY + ";");
        StringBuilder mss = new StringBuilder();
        if (DemoConfig.ROUTE_TEST_CASE_WIDTH > 0) {
            mss.append("#route::case { line-color: ").append(DemoConfig.ROUTE_TEST_CASE_COLOR)
               .append("; line-width: ").append(DemoConfig.ROUTE_TEST_CASE_WIDTH).append(";")
               .append(common).append(" }\n");
        }
        mss.append("#route { line-color: ").append(DemoConfig.ROUTE_TEST_COLOR)
           .append("; line-width: ").append(DemoConfig.ROUTE_TEST_WIDTH).append(";")
           .append(common).append(" }");
        return mss.toString();
    }

    // =============================================================================================
    // STYLE REGRESSION REPROS (DemoConfig.LAYER_BUGS)
    // The reported rules, kept as close to the report as they can be - the point is that the
    // CartoCSS shape is the same, not that it looks good. Every A/B the report names is a knob.
    // =============================================================================================

    /** A PUA glyph of assets/style/fonts/osm.ttf, standing in for the reported '[style.icon]'. */
    public static final String BUG_ICON_GLYPH = ICON_PEAK;

    /**
     * Style of the REGRESSION REPRO layer. Three rules, one per reported symptom:
     *
     *   #bugpoints  two label attachments on one point. The ::label text is reported to vanish at
     *               text-size <= 10 and to draw at 11, and to draw either way once the ::icon
     *               attachment is gone or the text moves out of ::label (BUG_ICON_MODE /
     *               BUG_LABEL_MODE are those two controls).
     *   #bugsel     the selection rule: a 'back/' instance under the main line. Adding
     *               back/line-opacity is reported to stop the main line being drawn - both live in
     *               ONE attachment, so they are one vt layer.
     *   #bugline    a translucent wide line (breaks at the joins), carrying line labels. Note what
     *               text-clip does here: mapnikvt defaults it to text-allow-overlap, so
     *               allow-overlap alone moves the text off the label path onto the clipped
     *               geometry path.
     */
    public static String bugStyle() {
        StringBuilder mss = new StringBuilder();

        // --- 1. two label attachments on one point -------------------------------------------
        mss.append("#bugpoints {\n");
        if (!"none".equalsIgnoreCase(DemoConfig.BUG_ICON_MODE)) {
            String iconName = "empty".equalsIgnoreCase(DemoConfig.BUG_ICON_MODE) ? "''" : "[icon]";
            mss.append("  ::icon {\n")
               .append("    text-placement: billboard;\n")
               .append("    text-placement-priority: 9;\n")
               .append("    text-name: ").append(iconName).append(";\n")
               .append("    text-size: ").append(DemoConfig.BUG_ICON_SIZE).append(";\n")
               .append("    text-face-name: 'osm';\n")
               .append("    text-halo-fill: #ffffff;\n")
               .append("    text-halo-radius: 1;\n")
               .append("    text-fill: #c0392b;\n")
               .append("    text-allow-overlap: true;\n")
               .append("    text-clip: false;\n")
               .append("  }\n");
        }
        boolean labelInAttachment = !"inline".equalsIgnoreCase(DemoConfig.BUG_LABEL_MODE);
        String labelIndent = labelInAttachment ? "    " : "  ";
        if (labelInAttachment) {
            mss.append("  ::label {\n");
        }
        mss.append(labelIndent).append("text-name: [label];\n")
           .append(labelIndent).append("text-size: ").append(DemoConfig.BUG_LABEL_SIZE).append(";\n")
           .append(labelIndent).append("text-face-name: 'osm';\n")
           .append(labelIndent).append("text-dx: 0;\n")
           .append(labelIndent).append("text-dy: 0;\n")
           .append(labelIndent).append("text-horizontal-alignment: middle;\n")
           .append(labelIndent).append("text-vertical-alignment: middle;\n")
           .append(labelIndent).append("text-placement-priority: 9;\n")
           .append(labelIndent).append("text-placement: billboard;\n")
           .append(labelIndent).append("text-fill: #1a1a1a;\n")
           .append(labelIndent).append("text-allow-overlap: true;\n")
           .append(labelIndent).append("text-clip: false;\n");
        if (labelInAttachment) {
            mss.append("  }\n");
        }
        mss.append("}\n");

        // --- 2. a 'back/' instance under the main line ----------------------------------------
        float width = DemoConfig.BUG_SEL_WIDTH;
        mss.append("#bugsel::selected {\n");
        if (!DemoConfig.BUG_LINE_BORDER) {
            mss.append("  back/line-color: ").append(DemoConfig.BUG_BACK_COLOR).append(";\n")
               .append("  back/line-width: ").append(width + 5f).append(";\n")
               .append("  back/line-join: round;\n")
               .append("  back/line-cap: round;\n");
            if (DemoConfig.BUG_BACK_OPACITY >= 0) {
                mss.append("  back/line-opacity: ").append(DemoConfig.BUG_BACK_OPACITY).append(";\n");
            }
        }
        mss.append("  line-join: miter;\n")
           .append("  line-cap: round;\n")
           .append("  line-color: #e2001a;\n")
           .append("  line-width: ").append(width + 2f).append(";\n");
        if (DemoConfig.BUG_LINE_BORDER) {
            // The same casing as ONE rule, drawn from the line's own buffer. 1.5 is half the
            // back/ pair's width difference, so the two forms should look alike.
            mss.append("  line-border-width: 1.5;\n")
               .append("  line-border-color: ").append(DemoConfig.BUG_BACK_COLOR).append(";\n");
        }
        mss.append("}\n");

        // --- 3+4. a translucent line, and its line labels --------------------------------------
        mss.append("#bugline {\n")
           .append("  line-color: ").append(DemoConfig.BUG_LINE_COLOR).append(";\n")
           .append("  line-width: ").append(DemoConfig.BUG_LINE_WIDTH).append(";\n")
           .append("  line-join: round;\n")
           .append("  line-cap: round;\n");
        if (DemoConfig.BUG_LINE_LABEL) {
            mss.append("  text-name: [text];\n")
               .append("  text-placement: ").append(DemoConfig.BUG_TEXT_PLACEMENT).append(";\n")
               .append("  text-fill: black;\n")
               .append("  text-spacing: 40;\n")
               .append("  text-wrap-before: true;\n")
               .append("  text-face-name: 'DIN Pro Medium';\n")
               .append("  text-size: ").append(DemoConfig.BUG_TEXT_SIZE).append(";\n")
               .append("  text-halo-fill: #ffffff;\n")
               .append("  text-halo-radius: 2;\n")
               .append("  text-dy: ").append(DemoConfig.BUG_TEXT_DY).append(";\n");
            if (DemoConfig.BUG_TEXT_SPACING > 0) {
                mss.append("  text-spacing: ").append(DemoConfig.BUG_TEXT_SPACING).append(";\n");
            }
            if (DemoConfig.BUG_TEXT_MIN_DISTANCE > 0) {
                mss.append("  text-min-distance: ").append(DemoConfig.BUG_TEXT_MIN_DISTANCE).append(";\n");
            }
            if (DemoConfig.BUG_TEXT_ALLOW_OVERLAP) {
                mss.append("  text-allow-overlap: true;\n");
            }
            if (!"unset".equalsIgnoreCase(DemoConfig.BUG_TEXT_CLIP)) {
                mss.append("  text-clip: ").append(DemoConfig.BUG_TEXT_CLIP).append(";\n");
            }
        }
        mss.append("}\n");

        // --- 5. the bridge / tunnel span --------------------------------------------------------
        // Their own vt layer names, so NoDrapeLayerFilter can take them out of the drape bake by
        // name: --es noDrape '^contour|bug(bridge|tunnel)'. Deliberately WIDE, so the shape is
        // readable from the start camera rather than a hairline on a hillside.
        mss.append("#bugbridge {\n")
           .append("  line-color: ").append(DemoConfig.BUG_BRIDGE_COLOR).append(";\n")
           .append("  line-width: ").append(DemoConfig.BUG_BRIDGE_WIDTH).append(";\n")
           .append("  line-join: round;\n")
           .append("  line-cap: butt;\n")
           .append("  line-elevation-mode: ").append(DemoConfig.BUG_BRIDGE_MODE).append(";\n")
           .append("}\n");
        mss.append("#bugtunnel {\n")
           .append("  line-color: ").append(DemoConfig.BUG_TUNNEL_COLOR).append(";\n")
           .append("  line-width: ").append(DemoConfig.BUG_BRIDGE_WIDTH).append(";\n")
           .append("  line-join: round;\n")
           .append("  line-cap: butt;\n")
           .append("  line-dasharray: 12, 8;\n")
           .append("  line-elevation-mode: ").append(DemoConfig.BUG_TUNNEL_MODE).append(";\n")
           .append("}");
        return mss.toString();
    }

    /** The CartoCSS is written here, the FONTS come from the APK asset package (as the POI style). */
    public static MBVectorTileDecoder createBugDecoder() {
        String css = bugStyle();
        Log.i(TAG, "bug repro style:\n" + css);
        AssetPackage pack = openAppAssets();
        return pack != null
                ? new MBVectorTileDecoder(new CartoCSSStyleSet(css, pack))
                : new MBVectorTileDecoder(new CartoCSSStyleSet(css));
    }

    /**
     * Style of the MANEUVER ARROW layer (DemoConfig.LAYER_MANEUVERS). ManeuverArrowBuilder
     * serves one LINE per arrow; the head is 'line-end-arrow', which the vt line tesselator builds
     * on the last vertex out of the same screen-space extrusion the line itself uses.
     *
     * That is why the casing works: the casing rule repeats the arrow properties with its own,
     * wider line, and the head grows about its incenter - so the border is as thick round the head
     * as it is along the shaft, and shaft and head are ONE shape with no seam between them.
     *
     * Everything scales together with the camera. The two widths are interpolated over
     * [view::zoom] - the LIVE camera zoom, re-evaluated every frame, not the tile's zoom - and the
     * head is a multiple of the width, so shaft, head and border shrink in step as the map zooms
     * out and the arrow keeps its shape instead of swallowing the junction.
     */
    public static String maneuverStyle(String headPath) {
        StringBuilder mss = new StringBuilder();
        // Whole SHAFT first, then the head over it, each part in its own attachment - an attachment
        // is drawn at the position of its FIRST rule. The head paints over the line, so it keeps
        // its outline where it lands on its own shaft (a U-turn, once the map is zoomed out enough)
        // instead of dissolving into it; and 'line-arrow-only' cuts a slot one line width wide out
        // of the head's base, so nothing of the head crosses the shaft it docks on and the two read
        // as a single polygon. The shaft rules carry no arrow: the line runs its full length under
        // the head.
        // Both modes need the casing's numbers scaled back, for the same reason: they are read
        // against ITS wider line. With a custom path the box is what scales, so the correction is
        // just the ratio of the widths - shrink the casing's box by it and both rules describe the
        // SAME skeleton, leaving the (casing - fill) / 2 offset to draw the border. Without it the
        // casing's head is 13/8 bigger on top of its offset, and the border comes out twice too
        // thick. The built-in triangle needs the incenter formula instead, because its numbers
        // describe the drawn shape rather than a skeleton.
        float scale = headPath.isEmpty()
                ? maneuverCasingArrowScale()
                : DemoConfig.MANEUVER_WIDTH / Math.max(1.0e-3f, DemoConfig.MANEUVER_CASING_WIDTH);
        if (DemoConfig.MANEUVER_CASING_WIDTH > 0) {
            mss.append("#maneuver::case { line-color: ").append(DemoConfig.MANEUVER_CASING_COLOR)
               .append("; line-width: ").append(maneuverWidthByZoom(DemoConfig.MANEUVER_CASING_WIDTH))
               .append("; line-join: round; line-cap: round; }\n");
        }
        mss.append("#maneuver::fill { line-color: ").append(DemoConfig.MANEUVER_COLOR)
           .append("; line-width: ").append(maneuverWidthByZoom(DemoConfig.MANEUVER_WIDTH))
           .append("; line-join: round; line-cap: round; }\n");
        if (DemoConfig.MANEUVER_CASING_WIDTH > 0) {
            mss.append("#maneuver::headcase { line-color: ").append(DemoConfig.MANEUVER_CASING_COLOR)
               .append("; line-width: ").append(maneuverWidthByZoom(DemoConfig.MANEUVER_CASING_WIDTH))
               .append(";").append(maneuverArrow(DemoConfig.MANEUVER_ARROW_WIDTH * scale,
                                                 DemoConfig.MANEUVER_ARROW_LENGTH * scale, headPath))
               .append(" }\n");
        }
        mss.append("#maneuver::head { line-color: ").append(DemoConfig.MANEUVER_COLOR)
           .append("; line-width: ").append(maneuverWidthByZoom(DemoConfig.MANEUVER_WIDTH))
           .append(";").append(maneuverArrow(DemoConfig.MANEUVER_ARROW_WIDTH,
                                             DemoConfig.MANEUVER_ARROW_LENGTH, headPath))
           .append(" }");
        return mss.toString();
    }

    private static String maneuverArrow(float arrowWidth, float arrowLength, String headPath) {
        // A custom path replaces the built-in triangle. It is a SKELETON, offset outward by half of
        // each rule's own line width, so BOTH rules use the same path and the casing lands
        // (casing - fill) / 2 outside the fill - the border the shaft has, for any shape. The
        // built-in triangle keeps its own route (grown about its incenter), which is the same
        // offset on a triangle and needs no path at all.
        // In path mode the two numbers are the BOX the contour is fitted into - length along the
        // line, width across it - so they still drive the size, and the path can come from any
        // viewBox.
        String shape = headPath.isEmpty()
                ? " line-arrow-width: " + arrowWidth + "; line-arrow-length: " + arrowLength + ";"
                : " line-arrow-width: " + arrowWidth + "; line-arrow-length: " + arrowLength
                  + "; line-arrow-scale: " + DemoConfig.MANEUVER_ARROW_SCALE
                  + "; line-arrow-rotation: " + DemoConfig.MANEUVER_ARROW_ROTATION
                  + "; line-arrow-path: '" + headPath + "';";
        return " line-join: round; line-cap: round; line-end-arrow: true; line-arrow-only: true;" + shape;
    }

    /**
     * What to multiply the fill's arrow numbers by for the casing rule, so the head keeps the
     * border the shaft has. The head's inradius is r = a*L / (a + hypot(a, L)) for a half-base a
     * and a length L; the casing head is the fill head grown by (casing - fill) / 2, which for a
     * triangle is the same shape scaled about its incenter - and the numbers are read against the
     * casing's own, wider line, hence the width ratio.
     */
    private static float maneuverCasingArrowScale() {
        float fill = DemoConfig.MANEUVER_WIDTH, casing = DemoConfig.MANEUVER_CASING_WIDTH;
        if (fill <= 0 || casing <= fill) {
            return 1;
        }
        double a = DemoConfig.MANEUVER_ARROW_WIDTH * fill / 2, l = DemoConfig.MANEUVER_ARROW_LENGTH * fill;
        double inradius = a * l / (a + Math.hypot(a, l));
        double grown = inradius + (casing - fill) / 2;
        return (float) (grown / inradius * fill / casing);
    }

    /** width at MANEUVER_ZOOM_REF, MIN_SCALE of it at MANEUVER_ZOOM_MIN, interpolated in between. */
    private static String maneuverWidthByZoom(float width) {
        return "linear([view::zoom], (" + DemoConfig.MANEUVER_ZOOM_MIN + ", "
                + (width * DemoConfig.MANEUVER_MIN_SCALE) + "), ("
                + DemoConfig.MANEUVER_ZOOM_REF + ", " + width + "))";
    }

    // =============================================================================================
    // SHADERS used by the hillshade / custom raster layers
    // =============================================================================================

    /**
     * Slope colouring: replaces the hillshade lighting with steepness bands (ski-touring style).
     * A custom normal-map lighting shader returns a PREMULTIPLIED colour and must be transparent
     * where it draws nothing, otherwise it greys out the map below.
     */
    public static String slopesShader() {
        return String.join("\n",
            "uniform vec4 u_shadowColor;",
            "uniform vec4 u_highlightColor;",
            "uniform vec4 u_accentColor;",
            "uniform vec3 u_lightDir;",
            "vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {",
            "    mediump float lighting = max(0.0, dot(normal, u_lightDir));",
            "    mediump float slope = acos(dot(normal, surfaceNormal)) * 180.0 / 3.14159 * 1.2;",
            "    if (slope >= 45.0) { return vec4(0.378, 0.272, 0.358, 0.5); }",
            "    if (slope >= 40.0) { return vec4(0.5, 0.0, 0.0, 0.5); }",
            "    if (slope >= 35.0) { return vec4(0.455, 0.231, 0.111, 0.5); }",
            "    if (slope >= 30.0) { return vec4(0.470, 0.451, 0.153, 0.5); }",
            "    return vec4(0.0, 0.0, 0.0, 0.0);",
            "}");
    }

    /**
     * Hypsometric tint for the CustomRasterTileLayer: decodes terrarium elevation from the RAW DEM
     * texel (getRawColor()) and colours it by height. Shows that the custom-raster base class can
     * run any filter shader over any raster source, not just hillshading.
     */
    public static String hypsometricShader() {
        return String.join("\n",
            "vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {",
            "  vec4 c = getRawColor();",
            "  float h = (c.r * 255.0 * 256.0 + c.g * 255.0 + c.b * 255.0 / 256.0) - 32768.0;",
            "  float t = clamp(h / 3000.0, 0.0, 1.0);",
            "  vec3 col = mix(vec3(0.2, 0.4, 0.8), vec3(0.9, 0.9, 0.4), t);",
            "  col = mix(col, vec3(0.5, 0.3, 0.1), clamp((h - 1500.0) / 1500.0, 0.0, 1.0));",
            "  return vec4(col, 1.0);",
            "}");
    }

    /**
     * Summit names, drawn as callout labels: the label is lifted to a band near the top of the
     * screen and joined back to the summit by a leader line, and a label that would collide is
     * moved one row up instead of being dropped ('callout' placement, see vt::LabelOrientation).
     * The layer name and fields are OpenMapTiles ('mountain_peak', name/ele/class).
     */
    public static String peaksStyle() {
        // The leader line always meets the FIRST letter of the name, which is also the point held
        // over the summit. What changes with the mode is the corner the row is aligned on: pinned
        // to the top the labels hang from their top right corner so the text stays under the
        // screen edge; in a band lower down they line up on the same bottom left corner they are
        // anchored by, and read up and to the right.
        String lineAnchor = DemoConfig.PEAKS_LINE_ANCHOR.isEmpty() ? "bottom-left" : DemoConfig.PEAKS_LINE_ANCHOR;
        String align = DemoConfig.PEAKS_ALIGN.isEmpty()
            ? (DemoConfig.PEAKS_PIN_TOP ? "top-right" : "bottom-left") : DemoConfig.PEAKS_ALIGN;
        return String.join("\n",
            "#mountain_peak['class'='peak'][zoom>=" + DemoConfig.PEAKS_MIN_ZOOM + "] {",
            "  text-name: [name];",
            // The elevation as a second run of text: same label, same plate, smaller font.
            "  text-secondary-name: [ele]+'m';",
            "  text-secondary-scale: " + DemoConfig.PEAKS_ELE_SCALE + ";",
            "  text-secondary-fill: " + hex(DemoConfig.PEAKS_ELE_COLOR_ARGB) + ";",
            "  text-secondary-dx: " + DemoConfig.PEAKS_ELE_GAP + ";",
            "  text-secondary-dy: " + DemoConfig.PEAKS_ELE_DY + ";",
            "  text-size: " + DemoConfig.PEAKS_TEXT_SIZE + ";",
            "  text-fill: " + hex(DemoMap.reliefInk()) + ";",
            "  text-halo-fill: " + hex(DemoMap.reliefPaper()) + ";",
            "  text-halo-radius: 1.5;",
            // The plate behind the name - a general label property, so a classic map style can use
            // exactly the same four lines.
            // The plate follows the palette too, so the names stay readable in both.
            "  text-background-fill: " + hex(DemoConfig.RELIEF_DARK ? DemoConfig.RELIEF_PAPER_DARK : DemoConfig.PEAKS_BG_COLOR_ARGB) + ";",
            "  text-background-opacity: " + DemoConfig.PEAKS_BG_OPACITY + ";",
            "  text-background-radius: " + DemoConfig.PEAKS_BG_RADIUS + ";",
            "  text-background-padding-x: " + DemoConfig.PEAKS_BG_PADDING_X + ";",
            "  text-background-padding-y: " + DemoConfig.PEAKS_BG_PADDING_Y + ";",
            "  text-placement: callout;",
            // The higher summit claims the row: without this the winner is whichever label the
            // tile order happened to offer first, and a 700 m hill hides a 2000 m one behind it.
            "  text-placement-priority: [ele];",
            DemoConfig.PEAKS_MIN_DISTANCE > 0 ? "  text-min-distance: " + DemoConfig.PEAKS_MIN_DISTANCE + ";" : "",
            // ... and the nearer of two summits of the same height wins the slot. text-rank is
            // evaluated per label by the culler, which is where view::distance means something.
            // No feature field in it on purpose: an expression that reads only the view state is
            // built ONCE and shared by every label.
            // '0 - x', not '-x': in CartoCSS a leading minus in front of a field is read as the
            // literal "-" (the parser's literal rule accepts '-' as a first character).
            DemoConfig.PEAKS_DISTANCE_RANK > 0 ? "  text-rank: [ele] + [view::distance]/" + DemoConfig.PEAKS_DISTANCE_RANK + ";" : "",
            "  text-orientation: " + DemoConfig.PEAKS_TEXT_ANGLE + ";",
            "  text-callout-line-anchor: " + lineAnchor + ";",
            "  text-callout-align: " + align + ";",
            "  text-callout-screen-anchor: " + (DemoConfig.PEAKS_PIN_TOP ? DemoConfig.PEAKS_TOP_OFFSET : DemoConfig.PEAKS_BAND) + ";",
            "  text-callout-offset: " + DemoConfig.PEAKS_MIN_OFFSET + ";",
            // Pinned to the top there is no room above the row, so the extra rows go DOWN.
            "  text-callout-step: " + (DemoConfig.PEAKS_PIN_TOP ? -DemoConfig.PEAKS_ROW_STEP : DemoConfig.PEAKS_ROW_STEP) + ";",
            "  text-callout-max-rows: " + DemoConfig.PEAKS_MAX_ROWS + ";",
            "  text-callout-persist: " + DemoConfig.PEAKS_PERSIST + ";",
            "  text-callout-line-width: " + DemoConfig.PEAKS_LINE_WIDTH + ";",
            DemoConfig.PEAKS_MAX_DISTANCE > 0 ? "  text-max-distance: " + DemoConfig.PEAKS_MAX_DISTANCE + ";" : "",
            "}");
    }

    /**
     * The relief (peak-finder) OUTLINE effect, as a fragment shader for PostProcessEffect:
     * silhouettes and creases reconstructed from the packed terrain depth the renderer hands the
     * effect. It lives here, not in the SDK, for the same reason the surface shader does - the SDK
     * provides the mechanism (an offscreen frame, a depth texture, named parameters) and the
     * application decides what the map looks like.
     * Parameters (PostProcessEffect.setFloatParameter / setColorParameter): uIntensity,
     * uOutlineWidth, uHorizonBoost, uDepthThreshold, uCreaseStrength, uDepthTexelSize,
     * uGrazingFloor, uDistanceFade, uHaze, uInkColor, uPaperColor.
     */
    public static String reliefOutlineShader() {
        return String.join("\n",
            "#version 100",
            "#ifdef GL_FRAGMENT_PRECISION_HIGH",
            "precision highp float;",
            "#else",
            "precision mediump float;",
            "#endif",
            "",
            "uniform sampler2D uColorTex;",
            "uniform sampler2D uTerrainDepthTex;",
            "uniform vec2 uInvScreenSize;",
            "uniform vec2 uProjInvScale;",
            "uniform float uFar;",
            "uniform float uIntensity;",
            "uniform float uOutlineWidth;",
            "uniform float uHorizonBoost;",
            "uniform float uDepthThreshold;",
            "uniform float uCreaseStrength;",
            "uniform float uDepthTexelSize;",
            "uniform float uGrazingFloor;",
            "uniform float uDistanceFade;",
            "uniform float uHaze;",
            "uniform vec4 uInkColor;",
            "uniform vec4 uPaperColor;",
            "",
            "float unpackDepth(vec4 c) {",
            "    return dot(c.rgb, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0));",
            "}",
            "",
            "// Eye-space position of a pixel from the packed linear depth.",
            "vec3 eyePos(vec2 uv, float depth) {",
            "    vec2 ndc = uv * 2.0 - 1.0;",
            "    return vec3(ndc * uProjInvScale, -1.0) * depth * uFar;",
            "}",
            "",
            "void main(void) {",
            "    vec2 uv = gl_FragCoord.xy * uInvScreenSize;",
            "    vec4 color = texture2D(uColorTex, uv);",
            "",
            "    vec4 c0 = texture2D(uTerrainDepthTex, uv);",
            "    float d0 = unpackDepth(c0);",
            "",
            "    // One width for the terrain-against-terrain lines, everywhere. Widening them with",
            "    // distance instead (the obvious reading of \"the horizon is bolder\") smears the",
            "    // far ranges into a solid band: up there the ridges are a pixel apart, so every",
            "    // pixel is inside some line. What is bold in a panorama is the SKY silhouette,",
            "    // and that gets its own, wider test below.",
            "    // Never narrower than uDepthTexelSize screen pixels: the terrain depth runs at",
            "    // half resolution with nearest filtering, so a narrower step samples the same",
            "    // texel twice and every comparison below degenerates.",
            "    vec2 delta = uInvScreenSize * max(uOutlineWidth, uDepthTexelSize);",
            "    vec2 skyDelta = uInvScreenSize * max(uOutlineWidth * (1.0 + uHorizonBoost), uDepthTexelSize);",
            "    vec4 cx0 = texture2D(uTerrainDepthTex, uv - vec2(delta.x, 0.0));",
            "    vec4 cx1 = texture2D(uTerrainDepthTex, uv + vec2(delta.x, 0.0));",
            "    vec4 cy0 = texture2D(uTerrainDepthTex, uv - vec2(0.0, delta.y));",
            "    vec4 cy1 = texture2D(uTerrainDepthTex, uv + vec2(0.0, delta.y));",
            "    float dx0 = unpackDepth(cx0);",
            "    float dx1 = unpackDepth(cx1);",
            "    float dy0 = unpackDepth(cy0);",
            "    float dy1 = unpackDepth(cy1);",
            "",
            "    // The local surface, from the four neighbours. Two things below need it: a",
            "    // surface seen edge-on legitimately changes depth fast from pixel to pixel, and a",
            "    // fold has to be told apart from a merely oblique slope.",
            "    vec3 p0 = eyePos(uv, d0);",
            "    vec3 tx0 = eyePos(uv - vec2(delta.x, 0.0), dx0) - p0;",
            "    vec3 tx1 = eyePos(uv + vec2(delta.x, 0.0), dx1) - p0;",
            "    vec3 ty0 = eyePos(uv - vec2(0.0, delta.y), dy0) - p0;",
            "    vec3 ty1 = eyePos(uv + vec2(0.0, delta.y), dy1) - p0;",
            "    // Two samples that landed on the same depth texel give a zero tangent, and",
            "    // normalizing that is undefined - it painted the whole near field grey.",
            "    float minLength = 1.0e-4 * d0 * uFar;",
            "    bool tangentsValid = length(tx1) > minLength && length(ty1) > minLength;",
            "    float grazing = 1.0;",
            "    if (tangentsValid) {",
            "        vec3 surfaceNormal = normalize(cross(tx1, ty1));",
            "        grazing = abs(dot(normalize(-p0), surfaceNormal));",
            "    }",
            "",
            "    // Silhouette: the line belongs to the NEARER side of a depth break, so only a",
            "    // neighbour FURTHER away counts. Testing the absolute difference draws the same",
            "    // ridge twice, once on each side, which at the horizon merges into a smear.",
            "    // The threshold is relative to the depth, or the far half of the view draws",
            "    // no line at all - and it is relaxed where the surface is seen EDGE-ON, because",
            "    // there the depth runs away between neighbouring pixels without anything being",
            "    // in front of anything: flat ground at its own horizon drew a solid black band.",
            "    float behind = max(max(dx0 - d0, dx1 - d0), max(dy0 - d0, dy1 - d0));",
            "    float threshold = uDepthThreshold * (0.0008 + 0.02 * d0) / max(grazing, uGrazingFloor);",
            "    float edge = smoothstep(threshold, threshold * 2.0, behind);",
            "    // Terrain-against-terrain lines fade with distance so that the horizon - the sky",
            "    // silhouette below, which does not fade - is the boldest line in the frame.",
            "    edge *= mix(1.0, uDistanceFade, d0);",
            "    // ...and terrain against the sky always is one (coverage, not depth: a sky pixel",
            "    // is at the far plane, which the relative threshold above would forgive). This is",
            "    // the horizon line, and it is the one that is drawn wide.",
            "    float skyNeighbour = 1.0 - min(",
            "        min(texture2D(uTerrainDepthTex, uv - vec2(skyDelta.x, 0.0)).a, texture2D(uTerrainDepthTex, uv + vec2(skyDelta.x, 0.0)).a),",
            "        min(texture2D(uTerrainDepthTex, uv - vec2(0.0, skyDelta.y)).a, texture2D(uTerrainDepthTex, uv + vec2(0.0, skyDelta.y)).a));",
            "    edge = max(edge, skyNeighbour * c0.a);",
            "",
            "    // Ridges and valleys: the two tangent directions away from this pixel point",
            "    // straight apart on a flat surface (dot -1) and fold together over a crest.",
            "    // Done on eye positions rather than on depth, so a merely oblique slope - which",
            "    // is most of a panorama - does not read as a fold.",
            "    float cover = min(min(cx0.a, cx1.a), min(cy0.a, cy1.a)) * c0.a;",
            "    if (uCreaseStrength > 0.0 && cover > 0.0) {",
            "        float fold = 0.0;",
            "        if (length(tx0) > minLength && length(tx1) > minLength) {",
            "            fold = max(fold, 1.0 + dot(normalize(tx0), normalize(tx1)));",
            "        }",
            "        if (length(ty0) > minLength && length(ty1) > minLength) {",
            "            fold = max(fold, 1.0 + dot(normalize(ty0), normalize(ty1)));",
            "        }",
            "        // Same reasoning as the silhouette threshold: an edge-on surface folds in",
            "        // projection without folding in the world.",
            "        edge = max(edge, smoothstep(0.05, 0.4, fold) * uCreaseStrength * grazing * mix(1.0, uDistanceFade, d0));",
            "    }",
            "",
            "    // Aerial perspective: the shaded surface fades into the paper with distance, so",
            "    // the far ranges read as pale outlines and the near ground keeps its shading.",
            "    vec3 shaded = mix(color.rgb, uPaperColor.rgb, uHaze * d0 * c0.a);",
            "    vec3 stylized = mix(shaded, uInkColor.rgb, edge * uInkColor.a);",
            "",
            "    gl_FragColor = vec4(mix(color.rgb, stylized, uIntensity), 1.0);",
            "}");
    }

    /**
     * Terrain surface shader for the relief (peak-finder) look: the shaded ground the outline
     * effect draws its ink lines over. Lambert shading between a paper and a shade colour, the
     * distance pulling everything back towards the paper, and the resolved fog on top - so a
     * panorama reads as a stack of ever paler ridges.
     * Parameters (TerrainOptions.setSurfaceParameter / setSurfaceColorParameter):
     * uPaperColor, uShadeColor, uShadeStrength, uAmbient, uHaze, uHazeDistance.
     */
    public static String reliefSurfaceShader() {
        return String.join("\n",
            "uniform vec4 uPaperColor;",
            "uniform vec4 uShadeColor;",
            "uniform float uShadeStrength;",
            "uniform float uAmbient;",
            "uniform float uHaze;",
            "uniform float uHazeDistance;",
            "vec4 surfaceColor() {",
            "    vec3 n = normalize(v_normal);",
            "    float lambert = max(dot(n, normalize(u_sunDir)), 0.0);",
            "    float light = mix(uAmbient, 1.0, lambert);",
            "    vec3 color = mix(uShadeColor.rgb, uPaperColor.rgb, clamp(1.0 - uShadeStrength * (1.0 - light), 0.0, 1.0));",
            "    color = mix(color, uPaperColor.rgb, clamp(v_dist / max(uHazeDistance, 1.0), 0.0, 1.0) * uHaze);",
            // No fog here: the SDK applies the frame's own fog to whatever surfaceColor returns.
            "    return vec4(color, 1.0);",
            "}");
    }

    private DemoStyles() {
    }
}
