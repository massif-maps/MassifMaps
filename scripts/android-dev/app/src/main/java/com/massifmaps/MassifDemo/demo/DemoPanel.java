package com.massifmaps.MassifDemo.demo;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import java.util.ArrayList;
import java.util.List;

/**
 * The on-screen settings panel: every {@link DemoConfig} knob, live.
 *
 * The panel NEVER touches SDK objects directly. It writes a DemoConfig field and then calls the
 * matching apply/rebuild method on {@link DemoMap}, which is the only place that knows how a
 * config value maps onto the SDK. Adding a knob is therefore: add the field in DemoConfig, apply
 * it in DemoMap.apply*(), add one line here.
 *
 * LAYOUT. There are far too many knobs to scroll through, so:
 *  - they live in COLLAPSIBLE sections, and opening one closes the others - the list of section
 *    titles is short enough to read at a glance, which the flat list never was;
 *  - a FILTER box at the top matches on the label of every row, across all sections, and shows
 *    the matches expanded. Typing "shadow" is faster than remembering which section it is in.
 *
 * Everything is built in code (no XML): a knob is one line here and nothing else.
 * '--es ui false' skips the whole thing, for clean screenshots.
 */
public final class DemoPanel {

    private interface BoolSetting { void set(boolean value); }
    private interface FloatSetting { void set(float value); }
    private interface Action { void run(); }
    private interface IntSetting { void set(int index); }

    // --- looks ------------------------------------------------------------------------------------
    private static final int COLOR_PANEL = 0xFF10151C;
    private static final int COLOR_SECTION = 0xFF1E2731;
    private static final int COLOR_TEXT = 0xFFE6EAF0;
    private static final int COLOR_DIM = 0xFF97A2B0;
    private static final int COLOR_ACCENT = 0xFF35D6C0;

    /** Live "z=.. tilt=.." readout, updated by the fragment's map listener. */
    public static TextView statusText;
    /** Shows which style was actually loaded (dir / zip / inline / project). */
    private static TextView styleText;
    /** Shows, per composite slot, whether the style actually declares it. */
    private static TextView slotText;

    private static float density = 1f;
    private static TextView gearButton;
    private static LinearLayout elevationWidget;   // viewpoint elevation, peak-finder mode only
    private static TextView elevationText;
    private static LinearLayout sections;      // the column every section is added to
    private static LinearLayout currentContent; // the section rows are currently going into
    private static final List<Row> rows = new ArrayList<Row>();
    private static final List<Section> sectionList = new ArrayList<Section>();

    /** One filterable row: the view to hide, and the text a filter matches against. */
    private static final class Row {
        final View view;
        final String text;
        final Section section;
        Row(View view, String text, Section section) {
            this.view = view;
            this.text = text;
            this.section = section;
        }
    }

    private static final class Section {
        final LinearLayout container;
        final TextView title;
        final LinearLayout content;
        final String name;
        boolean expanded;
        Section(LinearLayout container, TextView title, LinearLayout content, String name) {
            this.container = container;
            this.title = title;
            this.content = content;
            this.name = name;
        }
    }

    /** Re-runs the slot check and updates both status lines. */
    private static void refreshStatus(DemoMap demo) {
        demo.checkCompositeSlots();
        if (slotText != null) {
            slotText.setText(demo.compositeStatus);
        }
        if (styleText != null) {
            styleText.setText("style: " + DemoStyles.lastLoadedDescription);
        }
    }

    public static void build(final Context context, View root, final DemoMap demo) {
        final ConstraintLayout parent = (ConstraintLayout) root;
        density = context.getResources().getDisplayMetrics().density;

        // The readout: bottom left, clear of the navigation bar. It is the view the layout already
        // holds (the fragment writes to it), moved and restyled.
        final TextView readout = (TextView) parent.findViewById(com.massifmaps.MassifDemo.R.id.zoomText);
        if (readout != null) {
            readout.setTextColor(COLOR_TEXT);
            readout.setTextSize(12);
            readout.setTypeface(Typeface.MONOSPACE);
            readout.setBackground(rounded(0xB3000000, 10));
            readout.setPadding(dp(10), dp(6), dp(10), dp(6));
            ConstraintLayout.LayoutParams lp = (ConstraintLayout.LayoutParams) readout.getLayoutParams();
            lp.topToTop = ConstraintLayout.LayoutParams.UNSET;
            lp.endToEnd = ConstraintLayout.LayoutParams.UNSET;
            lp.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID;
            lp.startToStart = ConstraintLayout.LayoutParams.PARENT_ID;
            lp.leftMargin = dp(12);
            lp.bottomMargin = dp(12);
            readout.setLayoutParams(lp);
        }

        if (!DemoConfig.UI_ENABLED) {
            applyInsets(parent, null, readout, null);
            return;
        }

        rows.clear();
        sectionList.clear();

        // --- the sheet -----------------------------------------------------------------------
        final LinearLayout sheet = new LinearLayout(context);
        sheet.setOrientation(LinearLayout.VERTICAL);
        sheet.setBackground(roundedTop(COLOR_PANEL, 20));
        sheet.setVisibility(View.GONE);
        sheet.setClickable(true); // swallow taps so they never reach the map

        LinearLayout titleRow = new LinearLayout(context);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        titleRow.setPadding(dp(14), dp(10), dp(8), dp(4));

        final EditText filter = new EditText(context);
        filter.setHint("filter settings");
        filter.setHintTextColor(COLOR_DIM);
        filter.setTextColor(COLOR_TEXT);
        filter.setTextSize(14);
        filter.setSingleLine(true);
        filter.setBackground(rounded(0x33FFFFFF, 10));
        filter.setPadding(dp(12), dp(8), dp(12), dp(8));
        titleRow.addView(filter, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView close = new TextView(context);
        close.setText("\u2715");
        close.setTextColor(COLOR_DIM);
        close.setTextSize(18);
        close.setPadding(dp(14), dp(6), dp(14), dp(6));
        close.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { showSheet(sheet, false); }
        });
        titleRow.addView(close);
        sheet.addView(titleRow);

        statusText = new TextView(context);
        statusText.setText("zoom -");
        statusText.setTextColor(COLOR_DIM);
        statusText.setTextSize(11);
        statusText.setPadding(dp(16), 0, dp(16), dp(2));
        sheet.addView(statusText);

        // Which style actually loaded: a status line of the sheet itself, not a filterable row.
        styleText = new TextView(context);
        styleText.setText("style: " + DemoStyles.lastLoadedDescription);
        styleText.setTextColor(COLOR_DIM);
        styleText.setTextSize(11);
        styleText.setPadding(dp(16), 0, dp(16), dp(6));
        sheet.addView(styleText);

        ScrollView scroll = new ScrollView(context);
        scroll.setFillViewport(true);
        sections = new LinearLayout(context);
        sections.setOrientation(LinearLayout.VERTICAL);
        sections.setPadding(dp(10), dp(4), dp(10), dp(16));
        scroll.addView(sections);
        sheet.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        buildLayerSection(context, demo);
        buildCompositeSection(context, demo);
        buildShieldSection(context, demo);
        buildTerrainSection(context, demo);
        buildHillshadeSection(context, demo);
        buildContourSection(context, demo);
        buildRouteTestSection(context, demo);
        buildRouteSelectSection(context, demo);
        buildBugSection(context, demo);
        buildSunSection(context, demo);
        buildCelestialSection(context, demo);
        buildSkyFogSection(context, demo);
        buildReliefSection(context, demo);
        buildDebugSection(context, demo);
        buildActionsSection(context, demo);

        filter.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int a, int b, int c) { }
            public void onTextChanged(CharSequence s, int a, int b, int c) { applyFilter(s.toString()); }
            public void afterTextChanged(Editable s) { }
        });

        ConstraintLayout.LayoutParams sheetParams = new ConstraintLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0);
        sheetParams.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID;
        sheetParams.startToStart = ConstraintLayout.LayoutParams.PARENT_ID;
        sheetParams.endToEnd = ConstraintLayout.LayoutParams.PARENT_ID;
        sheetParams.matchConstraintPercentHeight = 0.68f;
        sheetParams.topToTop = ConstraintLayout.LayoutParams.PARENT_ID;
        sheetParams.verticalBias = 1f;
        parent.addView(sheet, sheetParams);

        // --- the button that opens it ---------------------------------------------------------
        gearButton = new TextView(context);
        final TextView gear = gearButton;
        gear.setText("\u2699");
        gear.setTextSize(22);
        gear.setTextColor(COLOR_TEXT);
        gear.setGravity(Gravity.CENTER);
        gear.setBackground(rounded(0xCC1E2731, 28));
        gear.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                showSheet(sheet, sheet.getVisibility() != View.VISIBLE);
            }
        });
        // Viewpoint elevation, the peak-finder control: a column on the right edge, up/down and a
        // readout. Only shown in that mode - see setElevationWidgetVisible.
        elevationWidget = new LinearLayout(context);
        elevationWidget.setOrientation(LinearLayout.VERTICAL);
        elevationWidget.setGravity(Gravity.CENTER_HORIZONTAL);
        elevationWidget.setBackground(rounded(0xCC1E2731, 22));
        elevationWidget.setPadding(dp(4), dp(6), dp(4), dp(6));
        elevationWidget.setVisibility(DemoConfig.PEAK_FINDER ? View.VISIBLE : View.GONE);
        elevationText = new TextView(context);
        elevationText.setTextColor(COLOR_TEXT);
        elevationText.setTextSize(11);
        elevationText.setTypeface(Typeface.MONOSPACE);
        elevationText.setGravity(Gravity.CENTER);
        elevationText.setText(String.format("%.0fm", DemoConfig.PEAK_FINDER_ELEVATION));
        elevationWidget.addView(elevationStep(context, demo, "\u25B2", +1));
        elevationWidget.addView(elevationText, new LinearLayout.LayoutParams(dp(52), ViewGroup.LayoutParams.WRAP_CONTENT));
        elevationWidget.addView(elevationStep(context, demo, "\u25BC", -1));
        ConstraintLayout.LayoutParams elevParams = new ConstraintLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        elevParams.topToTop = ConstraintLayout.LayoutParams.PARENT_ID;
        elevParams.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID;
        elevParams.endToEnd = ConstraintLayout.LayoutParams.PARENT_ID;
        elevParams.rightMargin = dp(12);
        parent.addView(elevationWidget, elevParams);

        ConstraintLayout.LayoutParams gearParams = new ConstraintLayout.LayoutParams(dp(48), dp(48));
        gearParams.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID;
        gearParams.endToEnd = ConstraintLayout.LayoutParams.PARENT_ID;
        gearParams.rightMargin = dp(12);
        gearParams.bottomMargin = dp(12);
        parent.addView(gear, gearParams);

        applyInsets(parent, sheet, readout, gear);
    }

    /** One arrow of the elevation column. */
    private static TextView elevationStep(Context context, final DemoMap demo, String glyph, final int direction) {
        TextView button = new TextView(context);
        button.setText(glyph);
        button.setTextSize(18);
        button.setTextColor(COLOR_TEXT);
        button.setGravity(Gravity.CENTER);
        button.setPadding(dp(10), dp(8), dp(10), dp(8));
        button.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                DemoConfig.PEAK_FINDER_ELEVATION = Math.max(0, DemoConfig.PEAK_FINDER_ELEVATION
                        + direction * DemoConfig.PEAK_FINDER_ELEVATION_STEP);
                if (elevationText != null) {
                    elevationText.setText(String.format("%.0fm", DemoConfig.PEAK_FINDER_ELEVATION));
                }
                demo.applyViewpointElevation();
            }
        });
        return button;
    }

    /** Shown only in peak-finder mode; DemoMap calls this when the mode is switched. */
    /** Repaints the elevation readout from DemoConfig - for whoever moves the viewpoint without
     *  touching the buttons (the peak-finder fly-in animates it). */
    public static void refreshElevationLabel() {
        if (elevationText != null) {
            elevationText.setText(String.format("%.0fm", DemoConfig.PEAK_FINDER_ELEVATION));
        }
    }

    public static void setElevationWidgetVisible(final boolean visible) {
        final LinearLayout widget = elevationWidget;
        if (widget == null) {
            return;
        }
        widget.post(new Runnable() {
            public void run() {
                widget.setVisibility(visible ? View.VISIBLE : View.GONE);
                if (elevationText != null) {
                    elevationText.setText(String.format("%.0fm", DemoConfig.PEAK_FINDER_ELEVATION));
                }
            }
        });
    }

    /** The sheet and the button that opens it are the same control: only one shows at a time. */
    private static void showSheet(View sheet, boolean show) {
        sheet.setVisibility(show ? View.VISIBLE : View.GONE);
        if (gearButton != null) {
            gearButton.setVisibility(show ? View.GONE : View.VISIBLE);
        }
        if (show) {
            sheet.setAlpha(0f);
            sheet.setTranslationY(dp(24));
            sheet.animate().alpha(1f).translationY(0f).setDuration(160).start();
        }
    }

    /**
     * Keeps the overlay clear of the status and navigation bars. The MAP is deliberately left
     * edge to edge - it is the content.
     */
    private static void applyInsets(final ConstraintLayout parent, final View sheet,
                                    final View readout, final View gear) {
        ViewCompat.setOnApplyWindowInsetsListener(parent, new androidx.core.view.OnApplyWindowInsetsListener() {
            public WindowInsetsCompat onApplyWindowInsets(View v, WindowInsetsCompat windowInsets) {
                Insets bars = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars());
                Insets ime = windowInsets.getInsets(WindowInsetsCompat.Type.ime());
                if (sheet != null) {
                    // The keyboard is an inset too: the filter box is at the TOP of the sheet, so
                    // the rows below it have to move up out of the way rather than under it.
                    sheet.setPadding(0, 0, 0, Math.max(bars.bottom, ime.bottom));
                }
                for (View view : new View[] { readout, gear }) {
                    if (view != null && view.getLayoutParams() instanceof ViewGroup.MarginLayoutParams) {
                        ViewGroup.MarginLayoutParams lp = (ViewGroup.MarginLayoutParams) view.getLayoutParams();
                        lp.bottomMargin = bars.bottom + dp(12);
                        lp.leftMargin = bars.left + dp(12);
                        lp.rightMargin = bars.right + dp(12);
                        view.setLayoutParams(lp);
                    }
                }
                return windowInsets;
            }
        });
        ViewCompat.requestApplyInsets(parent);
    }

    /** Shows only the rows whose label matches, and opens the sections that have any. */
    private static void applyFilter(String query) {
        String needle = query.trim().toLowerCase();
        for (Section section : sectionList) {
            boolean any = false;
            for (Row row : rows) {
                if (row.section != section) {
                    continue;
                }
                boolean match = needle.isEmpty() || row.text.contains(needle)
                        || section.name.toLowerCase().contains(needle);
                row.view.setVisibility(match ? View.VISIBLE : View.GONE);
                any = any || match;
            }
            section.container.setVisibility(any ? View.VISIBLE : View.GONE);
            if (!needle.isEmpty()) {
                expand(section, any);
            } else {
                expand(section, section.expanded);
            }
        }
    }

    private static void expand(Section section, boolean expanded) {
        section.content.setVisibility(expanded ? View.VISIBLE : View.GONE);
        section.title.setText((expanded ? "\u25be  " : "\u25b8  ") + section.name);
        section.title.setTextColor(expanded ? COLOR_ACCENT : COLOR_TEXT);
    }

    // =============================================================================================
    // SECTIONS
    // =============================================================================================

    /** Add / remove whole layers, and pick what the base map is built from. */
    private static void buildLayerSection(Context context, final DemoMap demo) {
        header(context, "LAYERS");
        for (final DemoMap.Feature feature : DemoMap.Feature.values()) {
            check(context, feature.name().toLowerCase(), demo.isEnabled(feature), new BoolSetting() {
                public void set(boolean value) { demo.setEnabled(feature, value); }
            });
        }

        header(context, "BASE MAP");
        // Switching either of these rebuilds the base layer with a new decoder / layer class.
        choice(context, "mode", enumNames(DemoConfig.BaseMode.values()), DemoConfig.BASE_MODE.ordinal(), new IntSetting() {
            public void set(int index) {
                DemoConfig.BASE_MODE = DemoConfig.BaseMode.values()[index];
                demo.rebuildBaseLayer();
                refreshStatus(demo);
            }
        });
        choice(context, "style", enumNames(DemoConfig.StyleSource.values()), DemoConfig.STYLE_SOURCE.ordinal(), new IntSetting() {
            public void set(int index) {
                DemoConfig.STYLE_SOURCE = DemoConfig.StyleSource.values()[index];
                demo.rebuildBaseLayer();
                refreshStatus(demo);
            }
        });
        // A converted Mapbox Standard writes one project per light preset over a shared
        // style.mss, so this is a re-compile of the style, not an option apply.
        choice(context, "light", LIGHT_PRESETS, indexOf(LIGHT_PRESETS, DemoConfig.LIGHT_PRESET), new IntSetting() {
            public void set(int index) {
                DemoConfig.LIGHT_PRESET = LIGHT_PRESETS[index];
                demo.rebuildBaseLayer();
                refreshStatus(demo);
            }
        });
    }

    /** The projects a converted Mapbox Standard writes; "" lets the package pick its first. */
    private static final String[] LIGHT_PRESETS = { "", "day", "dawn", "dusk", "night" };

    /**
     * The shield test style (StyleSource.POI): a font icon on every POI and the name on whichever
     * side is free. Every knob here rebuilds the base layer, because all of them are style text -
     * the style is compiled once, when the decoder is built.
     */
    private static void buildShieldSection(Context context, final DemoMap demo) {
        header(context, "SHIELDS (style 'POI')");
        check(context, "use the shield test style", DemoConfig.STYLE_SOURCE == DemoConfig.StyleSource.POI, new BoolSetting() {
            public void set(boolean value) {
                DemoConfig.STYLE_SOURCE = (value ? DemoConfig.StyleSource.POI : DemoConfig.StyleSource.INLINE);
                demo.rebuildBaseLayer();
                refreshStatus(demo);
            }
        });
        check(context, "name on the free side", !DemoConfig.POI_ANCHORS.isEmpty(), new BoolSetting() {
            public void set(boolean value) { DemoConfig.POI_ANCHORS = (value ? "right,left,top,bottom" : ""); demo.rebuildBaseLayer(); }
        });
        check(context, "icon alone when nothing fits", DemoConfig.POI_TEXT_OPTIONAL, new BoolSetting() {
            public void set(boolean value) { DemoConfig.POI_TEXT_OPTIONAL = value; demo.rebuildBaseLayer(); }
        });
        check(context, "font icon", DemoConfig.POI_FONT_ICON, new BoolSetting() {
            public void set(boolean value) { DemoConfig.POI_FONT_ICON = value; demo.rebuildBaseLayer(); }
        });
        check(context, "bitmap shield", DemoConfig.POI_BITMAP_ICON, new BoolSetting() {
            public void set(boolean value) { DemoConfig.POI_BITMAP_ICON = value; demo.rebuildBaseLayer(); }
        });
        check(context, "plate behind the name", DemoConfig.POI_TEXT_BG, new BoolSetting() {
            public void set(boolean value) { DemoConfig.POI_TEXT_BG = value; demo.rebuildBaseLayer(); }
        });
        check(context, "plate behind the icon", DemoConfig.POI_ICON_BG, new BoolSetting() {
            public void set(boolean value) { DemoConfig.POI_ICON_BG = value; demo.rebuildBaseLayer(); }
        });
        slider(context, "plate radius", 0f, 20f, DemoConfig.POI_BG_RADIUS, false, new FloatSetting() {
            public void set(float value) { DemoConfig.POI_BG_RADIUS = value; demo.rebuildBaseLayer(); }
        });
        slider(context, "plate padding", 0f, 12f, DemoConfig.POI_BG_PADDING, false, new FloatSetting() {
            public void set(float value) { DemoConfig.POI_BG_PADDING = value; demo.rebuildBaseLayer(); }
        });
        slider(context, "plate border", 0f, 4f, DemoConfig.POI_BG_BORDER, false, new FloatSetting() {
            public void set(float value) { DemoConfig.POI_BG_BORDER = value; demo.rebuildBaseLayer(); }
        });
        slider(context, "gap icon/name", 0f, 12f, DemoConfig.POI_TEXT_DX, false, new FloatSetting() {
            public void set(float value) { DemoConfig.POI_TEXT_DX = value; demo.rebuildBaseLayer(); }
        });
    }

    /** Sources woven INTO the base style (CompositeVectorTileLayer only). */
    private static void buildCompositeSection(Context context, final DemoMap demo) {
        header(context, "COMPOSITE SLOTS");
        // A slot only exists if the STYLE declares a layer with that name; otherwise the source is
        // registered but never drawn. This line says which of the two it is, per slot.
        slotText = label(context, demo.compositeStatus);
        check(context, "#hillshade", DemoConfig.COMPOSITE_HILLSHADE, new BoolSetting() {
            public void set(boolean value) { DemoConfig.COMPOSITE_HILLSHADE = value; demo.syncCompositeSources(); refreshStatus(demo); }
        });
        check(context, "#satellite", DemoConfig.COMPOSITE_SATELLITE, new BoolSetting() {
            public void set(boolean value) { DemoConfig.COMPOSITE_SATELLITE = value; demo.syncCompositeSources(); refreshStatus(demo); }
        });
        check(context, "#contour", DemoConfig.COMPOSITE_CONTOUR, new BoolSetting() {
            public void set(boolean value) { DemoConfig.COMPOSITE_CONTOUR = value; demo.syncCompositeSources(); refreshStatus(demo); }
        });
        check(context, "single-pass rendering", DemoConfig.COMPOSITE_SINGLE_PASS, new BoolSetting() {
            public void set(boolean value) {
                DemoConfig.COMPOSITE_SINGLE_PASS = value;
                if (demo.compositeLayer != null) {
                    demo.compositeLayer.setSinglePassRenderingEnabled(value);
                }
            }
        });
        // +1 = fetch the DEM one zoom level deeper than the base map.
        slider(context, "#hillshade zoom bias", -2, 2, DemoConfig.COMPOSITE_HILLSHADE_ZOOM_BIAS, true, new FloatSetting() {
            public void set(float value) {
                DemoConfig.COMPOSITE_HILLSHADE_ZOOM_BIAS = Math.round(value);
                demo.syncCompositeSources();
            }
        });
    }

    private static void buildTerrainSection(Context context, final DemoMap demo) {
        header(context, "TERRAIN");
        check(context, "3D terrain", DemoConfig.TERRAIN_ENABLED, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_ENABLED = value; demo.animateTerrain(value); }
        });
        check(context, "billboard occlusion", DemoConfig.TERRAIN_BILLBOARD_OCCLUSION, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_BILLBOARD_OCCLUSION = value; demo.terrainOptions.setBillboardOcclusionEnabled(value); }
        });
        slider(context, "occlusion tolerance", 0f, 0.5f, DemoConfig.TERRAIN_OCCLUSION_TOLERANCE, false, new FloatSetting() {
            public void set(float value) { DemoConfig.TERRAIN_OCCLUSION_TOLERANCE = value; demo.terrainOptions.setBillboardOcclusionTolerance(value); }
        });
        // Exaggeration and the resolutions re-tesselate / drop every cached tile texture, so they
        // are applied on release only - applying them per pixel of drag is a guaranteed stall.
        slider(context, "exaggeration", 0f, 3f, DemoConfig.TERRAIN_EXAGGERATION, true, new FloatSetting() {
            public void set(float value) { DemoConfig.TERRAIN_EXAGGERATION = value; demo.terrainOptions.setExaggeration(value); }
        });
        slider(context, "mesh resolution", 16, 192, DemoConfig.TERRAIN_MESH_RESOLUTION, true, new FloatSetting() {
            public void set(float value) {
                DemoConfig.TERRAIN_MESH_RESOLUTION = Math.max(16, ((int) value / 16) * 16);
                demo.terrainOptions.setMeshResolution(DemoConfig.TERRAIN_MESH_RESOLUTION);
            }
        });
        check(context, "drape fills (RTT)", DemoConfig.TERRAIN_DRAPE_FILLS, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_DRAPE_FILLS = value; demo.terrainOptions.setDrapeFillsEnabled(value); }
        });
        check(context, "drape lines", DemoConfig.TERRAIN_DRAPE_LINES, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_DRAPE_LINES = value; demo.terrainOptions.setDrapeLinesEnabled(value); }
        });
        slider(context, "drape resolution", 256, 2048, DemoConfig.TERRAIN_DRAPE_RESOLUTION, true, new FloatSetting() {
            public void set(float value) {
                DemoConfig.TERRAIN_DRAPE_RESOLUTION = Math.max(256, ((int) value / 256) * 256);
                demo.terrainOptions.setDrapeResolution(DemoConfig.TERRAIN_DRAPE_RESOLUTION);
            }
        });
        check(context, "tile edge stitching", DemoConfig.TERRAIN_TILE_EDGE_STITCHING, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_TILE_EDGE_STITCHING = value; demo.terrainOptions.setTileEdgeStitchingEnabled(value); }
        });
        check(context, "seamless tile edges", DemoConfig.TERRAIN_SEAMLESS_TILE_EDGES, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_SEAMLESS_TILE_EDGES = value; demo.terrainOptions.setSeamlessTileEdgesEnabled(value); }
        });
        check(context, "elevation prefetch", DemoConfig.TERRAIN_ELEVATION_PREFETCH, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_ELEVATION_PREFETCH = value; demo.terrainOptions.setElevationPrefetchEnabled(value); }
        });
        check(context, "background bitmap", DemoConfig.TERRAIN_BACKGROUND_BITMAP, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_BACKGROUND_BITMAP = value; demo.terrainOptions.setBackgroundBitmapEnabled(value); }
        });
    }

    /** Stand-alone hillshade layer (LAYERS > hillshade). */
    private static void buildHillshadeSection(Context context, final DemoMap demo) {
        header(context, "HILLSHADE LAYER");
        final String[] methods = { "STANDARD", "COMBINED", "IGOR", "MULTIDIRECTIONAL", "BASIC" };
        int current = 2;
        for (int i = 0; i < methods.length; i++) {
            if (methods[i].equals(DemoConfig.HILLSHADE_METHOD)) {
                current = i;
            }
        }
        choice(context, "method", methods, current, new IntSetting() {
            public void set(int index) { DemoConfig.HILLSHADE_METHOD = methods[index]; demo.applyHillshadeConfig(); }
        });
        slider(context, "contrast", 0f, 1f, DemoConfig.HILLSHADE_CONTRAST, false, new FloatSetting() {
            public void set(float value) { DemoConfig.HILLSHADE_CONTRAST = value; demo.applyHillshadeConfig(); }
        });
        slider(context, "height scale", 0f, 1f, DemoConfig.HILLSHADE_HEIGHT_SCALE, false, new FloatSetting() {
            public void set(float value) { DemoConfig.HILLSHADE_HEIGHT_SCALE = value; demo.applyHillshadeConfig(); }
        });
        slider(context, "exaggeration", 0f, 3f, DemoConfig.HILLSHADE_EXAGGERATION, false, new FloatSetting() {
            public void set(float value) { DemoConfig.HILLSHADE_EXAGGERATION = value; demo.applyHillshadeConfig(); }
        });
        slider(context, "illumination (deg)", 0, 360, DemoConfig.HILLSHADE_ILLUMINATION_DEGREES, false, new FloatSetting() {
            public void set(float value) { DemoConfig.HILLSHADE_ILLUMINATION_DEGREES = value; demo.applyHillshadeConfig(); }
        });
        check(context, "illumination follows map", DemoConfig.HILLSHADE_ILLUMINATION_FOLLOWS_MAP, new BoolSetting() {
            public void set(boolean value) { DemoConfig.HILLSHADE_ILLUMINATION_FOLLOWS_MAP = value; demo.applyHillshadeConfig(); }
        });
        check(context, "slope colouring shader", DemoConfig.HILLSHADE_SLOPES_SHADER, new BoolSetting() {
            public void set(boolean value) { DemoConfig.HILLSHADE_SLOPES_SHADER = value; demo.applyHillshadeConfig(); }
        });
        check(context, "shader contour lines", DemoConfig.HILLSHADE_CONTOUR_LINES, new BoolSetting() {
            public void set(boolean value) { DemoConfig.HILLSHADE_CONTOUR_LINES = value; demo.applyHillshadeConfig(); }
        });
        slider(context, "shader contour interval (m)", 10, 500, DemoConfig.HILLSHADE_CONTOUR_INTERVAL, true, new FloatSetting() {
            public void set(float value) { DemoConfig.HILLSHADE_CONTOUR_INTERVAL = value; demo.applyHillshadeConfig(); }
        });
        slider(context, "shader contour width", 0.2f, 3f, DemoConfig.HILLSHADE_CONTOUR_WIDTH, true, new FloatSetting() {
            public void set(float value) { DemoConfig.HILLSHADE_CONTOUR_WIDTH = value; demo.applyHillshadeConfig(); }
        });
    }

    /** Geometry contours (ContourTileDataSource): shared by the layer and the composite slot. */
    private static void buildContourSection(Context context, final DemoMap demo) {
        header(context, "CONTOUR SOURCE");
        // All of these re-generate tiles, so they apply on release and then drop the cached tiles.
        slider(context, "base interval (m)", 1, 100, DemoConfig.CONTOUR_BASE_INTERVAL, true, new FloatSetting() {
            public void set(float value) { DemoConfig.CONTOUR_BASE_INTERVAL = value; reloadContours(demo); }
        });
        slider(context, "resolution (samples)", 32, 256, DemoConfig.CONTOUR_RESOLUTION, true, new FloatSetting() {
            public void set(float value) { DemoConfig.CONTOUR_RESOLUTION = (int) value; reloadContours(demo); }
        });
        slider(context, "simplify tolerance (px)", 0, 5, DemoConfig.CONTOUR_SIMPLIFY_TOLERANCE, true, new FloatSetting() {
            public void set(float value) { DemoConfig.CONTOUR_SIMPLIFY_TOLERANCE = value; reloadContours(demo); }
        });
        slider(context, "min visible tile zoom", 0, 16, DemoConfig.CONTOUR_MIN_VISIBLE_ZOOM, true, new FloatSetting() {
            public void set(float value) { DemoConfig.CONTOUR_MIN_VISIBLE_ZOOM = (int) value; reloadContours(demo); }
        });
        check(context, "seamless edges", DemoConfig.CONTOUR_SEAMLESS_EDGES, new BoolSetting() {
            public void set(boolean value) { DemoConfig.CONTOUR_SEAMLESS_EDGES = value; reloadContours(demo); }
        });
    }

    private static void reloadContours(DemoMap demo) {
        demo.applyContourConfig();
        // Generated tiles are cached by the layers, so drop them to see the new parameters.
        demo.contourSource().notifyTilesChanged(true);
    }

    /** The selection bench: pick a route, or let it cycle, and watch what the change costs. */
    private static void buildRouteSelectSection(Context context, final DemoMap demo) {
        header(context, "ROUTE SELECT");
        button(context, "select next route", new Action() {
            public void run() { demo.selectNextRoute(); }
        });
        final String[] modes = { "value", "filter" };
        choice(context, "mode", modes, indexOf(modes, DemoConfig.ROUTE_SELECT_MODE), new IntSetting() {
            public void set(int index) { DemoConfig.ROUTE_SELECT_MODE = modes[index]; demo.rebuildLayers(); }
        });
        slider(context, "routes", 2, 40, DemoConfig.ROUTE_SELECT_COUNT, false, new FloatSetting() {
            public void set(float value) { DemoConfig.ROUTE_SELECT_COUNT = (int) value; demo.rebuildLayers(); }
        });
        slider(context, "cycle ms", 0, 5000, DemoConfig.ROUTE_SELECT_CYCLE_MS, false, new FloatSetting() {
            public void set(float value) { DemoConfig.ROUTE_SELECT_CYCLE_MS = (int) value; demo.rebuildLayers(); }
        });
    }

    /** Join / cap / opacity of the route test layer - the line tesselation bench. */
    private static void buildRouteTestSection(Context context, final DemoMap demo) {
        header(context, "ROUTE TEST");
        final String[] joins = { "miter", "bevel", "round" };
        choice(context, "join", joins, indexOf(joins, DemoConfig.ROUTE_TEST_JOIN), new IntSetting() {
            public void set(int index) { DemoConfig.ROUTE_TEST_JOIN = joins[index]; reloadRouteTest(demo); }
        });
        final String[] caps = { "butt", "square", "round" };
        choice(context, "cap", caps, indexOf(caps, DemoConfig.ROUTE_TEST_CAP), new IntSetting() {
            public void set(int index) { DemoConfig.ROUTE_TEST_CAP = caps[index]; reloadRouteTest(demo); }
        });
        slider(context, "width", 1, 30, DemoConfig.ROUTE_TEST_WIDTH, true, new FloatSetting() {
            public void set(float value) { DemoConfig.ROUTE_TEST_WIDTH = value; reloadRouteTest(demo); }
        });
        slider(context, "casing width", 0, 40, DemoConfig.ROUTE_TEST_CASE_WIDTH, true, new FloatSetting() {
            public void set(float value) { DemoConfig.ROUTE_TEST_CASE_WIDTH = value; reloadRouteTest(demo); }
        });
        slider(context, "miter limit", 1, 12, DemoConfig.ROUTE_TEST_MITER_LIMIT, true, new FloatSetting() {
            public void set(float value) { DemoConfig.ROUTE_TEST_MITER_LIMIT = value; reloadRouteTest(demo); }
        });
        slider(context, "opacity", 0.1f, 1, DemoConfig.ROUTE_TEST_OPACITY, true, new FloatSetting() {
            public void set(float value) { DemoConfig.ROUTE_TEST_OPACITY = value; reloadRouteTest(demo); }
        });
        final String[] opacityModes = { "geom", "layer" };
        choice(context, "opacity mode", opacityModes, indexOf(opacityModes, DemoConfig.ROUTE_TEST_OPACITY_MODE), new IntSetting() {
            public void set(int index) { DemoConfig.ROUTE_TEST_OPACITY_MODE = opacityModes[index]; reloadRouteTest(demo); }
        });
        slider(context, "simplify", 0, 16, DemoConfig.ROUTE_TEST_SIMPLIFY, true, new FloatSetting() {
            public void set(float value) { DemoConfig.ROUTE_TEST_SIMPLIFY = value; reloadRouteTest(demo); }
        });
    }

    /** The style is baked into the decoder, so the layer is rebuilt from scratch. */
    private static void reloadRouteTest(DemoMap demo) {
        demo.invalidate(DemoMap.Feature.ROUTE_TEST);
        demo.rebuildLayers();
    }

    /**
     * The four reported style regressions, each with the A/B the report gives. Turn the layer on
     * with the 'bugs' checkbox in LAYERS (or --es bugs true); the features sit on the start camera.
     */
    private static void buildBugSection(Context context, final DemoMap demo) {
        header(context, "STYLE BUGS");

        // 1. two label attachments on one point.
        final String[] iconModes = { "glyph", "empty", "none" };
        choice(context, "::icon", iconModes, indexOf(iconModes, DemoConfig.BUG_ICON_MODE), new IntSetting() {
            public void set(int index) { DemoConfig.BUG_ICON_MODE = iconModes[index]; reloadBugs(demo); }
        });
        final String[] labelModes = { "attachment", "inline" };
        choice(context, "label in", labelModes, indexOf(labelModes, DemoConfig.BUG_LABEL_MODE), new IntSetting() {
            public void set(int index) { DemoConfig.BUG_LABEL_MODE = labelModes[index]; reloadBugs(demo); }
        });
        slider(context, "label size", 6, 24, DemoConfig.BUG_LABEL_SIZE, true, new FloatSetting() {
            public void set(float value) { DemoConfig.BUG_LABEL_SIZE = value; reloadBugs(demo); }
        });
        slider(context, "icon size", 6, 40, DemoConfig.BUG_ICON_SIZE, true, new FloatSetting() {
            public void set(float value) { DemoConfig.BUG_ICON_SIZE = value; reloadBugs(demo); }
        });

        // 2. a 'back/' instance under the main line. -1 = no back/line-opacity at all.
        slider(context, "back/line-opacity (-1 off)", -1f, 1f, DemoConfig.BUG_BACK_OPACITY, false, new FloatSetting() {
            public void set(float value) { DemoConfig.BUG_BACK_OPACITY = value; reloadBugs(demo); }
        });
        slider(context, "selection width", 1, 20, DemoConfig.BUG_SEL_WIDTH, true, new FloatSetting() {
            public void set(float value) { DemoConfig.BUG_SEL_WIDTH = value; reloadBugs(demo); }
        });

        // 3+4. the translucent line and its labels.
        slider(context, "line width", 1, 30, DemoConfig.BUG_LINE_WIDTH, true, new FloatSetting() {
            public void set(float value) { DemoConfig.BUG_LINE_WIDTH = value; reloadBugs(demo); }
        });
        final String[] lineColors = { "#00000077", "#000000ff" };
        choice(context, "line colour", lineColors, indexOf(lineColors, DemoConfig.BUG_LINE_COLOR), new IntSetting() {
            public void set(int index) { DemoConfig.BUG_LINE_COLOR = lineColors[index]; reloadBugs(demo); }
        });
        check(context, "line labels", DemoConfig.BUG_LINE_LABEL, new BoolSetting() {
            public void set(boolean value) { DemoConfig.BUG_LINE_LABEL = value; reloadBugs(demo); }
        });
        check(context, "text-allow-overlap", DemoConfig.BUG_TEXT_ALLOW_OVERLAP, new BoolSetting() {
            public void set(boolean value) { DemoConfig.BUG_TEXT_ALLOW_OVERLAP = value; reloadBugs(demo); }
        });
        final String[] placements = { "line", "billboard-line", "billboard-line-repeat", "billboard", "point" };
        choice(context, "text-placement", placements, indexOf(placements, DemoConfig.BUG_TEXT_PLACEMENT), new IntSetting() {
            public void set(int index) { DemoConfig.BUG_TEXT_PLACEMENT = placements[index]; reloadBugs(demo); }
        });
        final String[] clipModes = { "unset", "true", "false" };
        choice(context, "text-clip", clipModes, indexOf(clipModes, DemoConfig.BUG_TEXT_CLIP), new IntSetting() {
            public void set(int index) { DemoConfig.BUG_TEXT_CLIP = clipModes[index]; reloadBugs(demo); }
        });
        slider(context, "text size", 6, 30, DemoConfig.BUG_TEXT_SIZE, true, new FloatSetting() {
            public void set(float value) { DemoConfig.BUG_TEXT_SIZE = value; reloadBugs(demo); }
        });
        slider(context, "text-spacing (0 = off)", 0, 400, DemoConfig.BUG_TEXT_SPACING, true, new FloatSetting() {
            public void set(float value) { DemoConfig.BUG_TEXT_SPACING = value; reloadBugs(demo); }
        });
        slider(context, "text-min-distance (0 = off)", 0, 200, DemoConfig.BUG_TEXT_MIN_DISTANCE, true, new FloatSetting() {
            public void set(float value) { DemoConfig.BUG_TEXT_MIN_DISTANCE = value; reloadBugs(demo); }
        });
    }

    /** The style is baked into the decoder, so the layer is rebuilt from scratch. */
    private static void reloadBugs(DemoMap demo) {
        demo.invalidate(DemoMap.Feature.BUGS);
        demo.rebuildLayers();
    }

    private static int indexOf(String[] options, String value) {
        for (int i = 0; i < options.length; i++) {
            if (options[i].equalsIgnoreCase(value)) {
                return i;
            }
        }
        return 0;
    }

    private static void buildSunSection(Context context, final DemoMap demo) {
        header(context, "SUN");
        // A style may state its own sun - a converted MapBox style states one per light preset -
        // and by default that wins, so the sliders below do nothing until this is ticked.
        check(context, "app sun overrides style", DemoConfig.APP_SUN, new BoolSetting() {
            public void set(boolean value) { DemoConfig.APP_SUN = value; demo.lightOptions.setSunOverridingStyle(value); }
        });
        check(context, "terrain lighting", DemoConfig.TERRAIN_LIGHTING, new BoolSetting() {
            public void set(boolean value) { DemoConfig.TERRAIN_LIGHTING = value; demo.lightOptions.setTerrainLightingEnabled(value); }
        });
        check(context, "day cycle (sun/moon/sky)", DemoConfig.DAY_CYCLE, new BoolSetting() {
            public void set(boolean value) { DemoConfig.DAY_CYCLE = value; demo.applyDayCycle(DemoConfig.DAY_CYCLE_HOUR); }
        });
        // With the day cycle on, the hour drives everything; otherwise it only moves the sun.
        slider(context, "hour (UTC)", 0, 24, DemoConfig.DAY_CYCLE_HOUR, false, new FloatSetting() {
            public void set(float value) {
                DemoConfig.DAY_CYCLE_HOUR = value;
                if (DemoConfig.DAY_CYCLE) {
                    demo.applyDayCycle(value);
                } else {
                    DemoConfig.SUN_HOUR_UTC = value;
                    demo.applyLightOptions(); // which also re-places the sky objects for that hour
                }
            }
        });
        slider(context, "azimuth", 0, 360, DemoConfig.SUN_AZIMUTH, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SUN_AZIMUTH = value; DemoConfig.SUN_HOUR_UTC = -1; demo.lightOptions.setSunAzimuth(value); }
        });
        slider(context, "altitude", -10, 90, DemoConfig.SUN_ALTITUDE, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SUN_ALTITUDE = value; DemoConfig.SUN_HOUR_UTC = -1; demo.lightOptions.setSunAltitude(value); }
        });
        slider(context, "sun intensity", 0, 2, DemoConfig.SUN_INTENSITY, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SUN_INTENSITY = value; demo.lightOptions.setSunIntensity(value); }
        });
        slider(context, "ambient", 0, 1, DemoConfig.AMBIENT_INTENSITY, false, new FloatSetting() {
            public void set(float value) { DemoConfig.AMBIENT_INTENSITY = value; demo.lightOptions.setAmbientIntensity(value); }
        });

        header(context, "SHADOWS");
        // Past 1 on purpose: 1 is the physical depth, and a bench wants to be able to exaggerate.
        slider(context, "strength", 0, 2, DemoConfig.SHADOW_STRENGTH, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SHADOW_STRENGTH = value; demo.lightOptions.setShadowStrength(value); }
        });
        slider(context, "softness (texels)", 0, 4, DemoConfig.SHADOW_SOFTNESS, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SHADOW_SOFTNESS = value; demo.lightOptions.setShadowSoftness(value); }
        });
        // Reallocates the shadow map atlas, so apply on release only.
        slider(context, "map size", 512, 4096, DemoConfig.SHADOW_MAP_SIZE, true, new FloatSetting() {
            public void set(float value) {
                DemoConfig.SHADOW_MAP_SIZE = Math.max(512, ((int) value / 512) * 512);
                demo.lightOptions.setShadowMapSize(DemoConfig.SHADOW_MAP_SIZE);
            }
        });
        slider(context, "cascades", 1, 4, DemoConfig.SHADOW_CASCADES, true, new FloatSetting() {
            public void set(float value) { DemoConfig.SHADOW_CASCADES = Math.round(value); demo.lightOptions.setShadowCascades(DemoConfig.SHADOW_CASCADES); }
        });
        slider(context, "distance (x focus, 0=auto)", 0, 12, DemoConfig.SHADOW_DISTANCE, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SHADOW_DISTANCE = value < 0.25f ? 0 : value; demo.lightOptions.setShadowDistance(DemoConfig.SHADOW_DISTANCE); }
        });
        slider(context, "caster margin (tiles)", 0, 6, DemoConfig.SHADOW_CASTER_MARGIN, true, new FloatSetting() {
            public void set(float value) { DemoConfig.SHADOW_CASTER_MARGIN = Math.round(value); demo.lightOptions.setShadowCasterMargin(DemoConfig.SHADOW_CASTER_MARGIN); }
        });
        slider(context, "depth bias (m)", 0f, 5f, DemoConfig.SHADOW_BIAS, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SHADOW_BIAS = value; demo.lightOptions.setShadowBias(value); }
        });
        slider(context, "normal offset (texels)", 0f, 8f, DemoConfig.SHADOW_NORMAL_OFFSET, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SHADOW_NORMAL_OFFSET = value; demo.lightOptions.setShadowNormalOffset(value); }
        });
    }

    /**
     * Everything placed in the SKY, and the camera control that makes it reachable.
     *
     * The two layers are ordinary layers (LAYERS > celestial / stars); what is here is the demo
     * content inside them, and the look-around mode - one finger looks instead of panning, and the
     * tilt may go NEGATIVE, which is the view pitching above the horizon.
     */
    private static void buildCelestialSection(Context context, final DemoMap demo) {
        header(context, "SKY OBJECTS");
        // off = the map gestures; look = one finger looks; fps = mouse look + two-finger move.
        final String[] roamModes = { "off", "look", "fps" };
        int currentRoam = 0;
        for (int i = 0; i < roamModes.length; i++) {
            if (roamModes[i].equals(DemoConfig.FREE_ROAM_MODE)) {
                currentRoam = i;
            }
        }
        choice(context, "free roam", roamModes, currentRoam, new IntSetting() {
            public void set(int index) { DemoConfig.FREE_ROAM_MODE = roamModes[index]; demo.applyLookRange(); }
        });
        slider(context, "look sensitivity (deg/inch)", 20, 200, DemoConfig.FREE_ROAM_LOOK_SENSITIVITY, false, new FloatSetting() {
            public void set(float value) { DemoConfig.FREE_ROAM_LOOK_SENSITIVITY = value; demo.applyLookRange(); }
        });
        slider(context, "move speed (x distance/inch)", 0.05f, 2f, DemoConfig.FREE_ROAM_MOVE_SPEED, false, new FloatSetting() {
            public void set(float value) { DemoConfig.FREE_ROAM_MOVE_SPEED = value; demo.applyLookRange(); }
        });
        // 0 stops at the horizon, which is what a map does; 90 reaches the zenith.
        slider(context, "look above horizon (deg)", 0, 90, DemoConfig.LOOK_UP_LIMIT, true, new FloatSetting() {
            public void set(float value) { DemoConfig.LOOK_UP_LIMIT = value; demo.applyLookRange(); }
        });

        check(context, "sun", DemoConfig.CELESTIAL_SUN, new BoolSetting() {
            public void set(boolean value) { DemoConfig.CELESTIAL_SUN = value; demo.updateSky(); }
        });
        check(context, "moon", DemoConfig.CELESTIAL_MOON, new BoolSetting() {
            public void set(boolean value) { DemoConfig.CELESTIAL_MOON = value; demo.updateSky(); }
        });
        check(context, "moon phase", DemoConfig.CELESTIAL_MOON_PHASE, new BoolSetting() {
            public void set(boolean value) { DemoConfig.CELESTIAL_MOON_PHASE = value; demo.updateSky(); }
        });
        check(context, "sun path today", DemoConfig.CELESTIAL_ARC, new BoolSetting() {
            public void set(boolean value) { DemoConfig.CELESTIAL_ARC = value; demo.updateSky(); }
        });
        check(context, "moon path today", DemoConfig.CELESTIAL_MOON_ARC, new BoolSetting() {
            public void set(boolean value) { DemoConfig.CELESTIAL_MOON_ARC = value; demo.updateSky(); }
        });

        header(context, "STARS");
        check(context, "star layer", DemoConfig.STARS, new BoolSetting() {
            public void set(boolean value) { demo.setEnabled(DemoMap.Feature.STARS, value); }
        });
        check(context, "stars", DemoConfig.STARS_STARS, new BoolSetting() {
            public void set(boolean value) { DemoConfig.STARS_STARS = value; demo.updateSky(); }
        });
        check(context, "constellations", DemoConfig.STARS_FIGURES, new BoolSetting() {
            public void set(boolean value) { DemoConfig.STARS_FIGURES = value; demo.updateSky(); }
        });
        check(context, "constellation names", DemoConfig.STARS_LABELS, new BoolSetting() {
            public void set(boolean value) { DemoConfig.STARS_LABELS = value; demo.updateSky(); }
        });
        check(context, "planets", DemoConfig.STARS_PLANETS, new BoolSetting() {
            public void set(boolean value) { DemoConfig.STARS_PLANETS = value; demo.updateSky(); }
        });
        check(context, "celestial equator", DemoConfig.STARS_EQUATOR, new BoolSetting() {
            public void set(boolean value) { DemoConfig.STARS_EQUATOR = value; demo.updateSky(); }
        });
        // No map at all: the layers leave the layer list, so this costs an empty map.
        check(context, "star sky (no map, transparent)", DemoConfig.STAR_SKY, new BoolSetting() {
            public void set(boolean value) { demo.applyStarSky(value); }
        });
        check(context, "follow device orientation", DemoConfig.STAR_SKY_ORIENTATION, new BoolSetting() {
            public void set(boolean value) { demo.setOrientationFollowing(value); }
        });
        // The camera preview goes BEHIND the transparent map: the sky over what the camera sees.
        check(context, "camera behind (AR sky)", DemoConfig.STAR_SKY_CAMERA, new BoolSetting() {
            public void set(boolean value) { demo.setCameraPreviewEnabled(value); }
        });
    }

    /**
     * The relief (peak-finder) look: a shaded terrain surface plus the outline effect over it.
     * The surface only shows where no tile layer paints, so switch the base map off in LAYERS.
     */
    private static void buildReliefSection(Context context, final DemoMap demo) {
        header(context, "RELIEF");
        // One switch for the whole view: the pieces below are independent, and each one on its own
        // looks like nothing happens (the surface hides under the map, the names need summits).
        check(context, "peak finder mode", DemoConfig.PEAK_FINDER, new BoolSetting() {
            // Entering it flies there - one camera move that pulls back, comes down at the
            // panorama's zoom and tilt, and lifts the viewpoint while the terrain loads.
            public void set(boolean value) { if (value) { demo.flyToPeakFinder(); } else { demo.setPeakFinderMode(false); } }
        });
        check(context, "relief surface", DemoConfig.RELIEF_SURFACE, new BoolSetting() {
            public void set(boolean value) { DemoConfig.RELIEF_SURFACE = value; demo.applyReliefSurface(); }
        });
        check(context, "relief outline effect", DemoConfig.RELIEF_OUTLINE, new BoolSetting() {
            public void set(boolean value) { demo.setReliefOutlineEnabled(value); }
        });
        check(context, "dark palette", DemoConfig.RELIEF_DARK, new BoolSetting() {
            public void set(boolean value) { demo.setReliefDark(value); }
        });
        check(context, "AR (over the camera)", DemoConfig.AR_MODE, new BoolSetting() {
            public void set(boolean value) { demo.setArMode(value); }
        });
        slider(context, "outline width (px)", 0.5f, 4, DemoConfig.RELIEF_OUTLINE_WIDTH, false, new FloatSetting() {
            public void set(float value) { DemoConfig.RELIEF_OUTLINE_WIDTH = value; demo.applyReliefOutlineParameters(); }
        });
        slider(context, "horizon boost", 0, 8, DemoConfig.RELIEF_HORIZON_BOOST, false, new FloatSetting() {
            public void set(float value) { DemoConfig.RELIEF_HORIZON_BOOST = value; demo.applyReliefOutlineParameters(); }
        });
        slider(context, "silhouette threshold", 0.1f, 4, DemoConfig.RELIEF_DEPTH_THRESHOLD, false, new FloatSetting() {
            public void set(float value) { DemoConfig.RELIEF_DEPTH_THRESHOLD = value; demo.applyReliefOutlineParameters(); }
        });
        slider(context, "ridge lines", 0, 1, DemoConfig.RELIEF_CREASE_STRENGTH, false, new FloatSetting() {
            public void set(float value) { DemoConfig.RELIEF_CREASE_STRENGTH = value; demo.applyReliefOutlineParameters(); }
        });
        slider(context, "shade strength", 0, 1, DemoConfig.RELIEF_SHADE_STRENGTH, false, new FloatSetting() {
            public void set(float value) { DemoConfig.RELIEF_SHADE_STRENGTH = value; demo.applyReliefSurface(); }
        });
        slider(context, "ambient", 0, 1, DemoConfig.RELIEF_AMBIENT, false, new FloatSetting() {
            public void set(float value) { DemoConfig.RELIEF_AMBIENT = value; demo.applyReliefSurface(); }
        });
        slider(context, "haze", 0, 1, DemoConfig.RELIEF_HAZE, false, new FloatSetting() {
            public void set(float value) {
                DemoConfig.RELIEF_HAZE = value;
                demo.applyReliefSurface();
                demo.applyReliefOutlineParameters();
            }
        });
        slider(context, "haze distance (m)", 5000, 200000, DemoConfig.RELIEF_HAZE_DISTANCE, false, new FloatSetting() {
            public void set(float value) { DemoConfig.RELIEF_HAZE_DISTANCE = value; demo.applyReliefSurface(); }
        });

        // The peak labels are style-driven, so every knob here rebuilds the layer with a new
        // style - hence applyOnRelease on the sliders.
        check(context, "peak names", DemoConfig.LAYER_PEAKS, new BoolSetting() {
            public void set(boolean value) { demo.setEnabled(DemoMap.Feature.PEAKS, value); }
        });
        slider(context, "peak label band (screen)", 0, 0.6f, DemoConfig.PEAKS_BAND, true, new FloatSetting() {
            public void set(float value) { DemoConfig.PEAKS_BAND = value; demo.rebuildPeaksLayer(); }
        });
        slider(context, "peak label angle", 0, 90, DemoConfig.PEAKS_TEXT_ANGLE, true, new FloatSetting() {
            public void set(float value) { DemoConfig.PEAKS_TEXT_ANGLE = value; demo.rebuildPeaksLayer(); }
        });
        slider(context, "peak row step (px)", 8, 60, DemoConfig.PEAKS_ROW_STEP, true, new FloatSetting() {
            public void set(float value) { DemoConfig.PEAKS_ROW_STEP = value; demo.rebuildPeaksLayer(); }
        });
        slider(context, "peak max distance (m)", 0, 300000, DemoConfig.PEAKS_MAX_DISTANCE, true, new FloatSetting() {
            public void set(float value) { DemoConfig.PEAKS_MAX_DISTANCE = value; demo.rebuildPeaksLayer(); }
        });
        check(context, "peak labels pinned to top", DemoConfig.PEAKS_PIN_TOP, new BoolSetting() {
            public void set(boolean value) { DemoConfig.PEAKS_PIN_TOP = value; demo.rebuildPeaksLayer(); }
        });
        // How far behind the terrain an anchor may sit and still be labelled. A summit ON the ridge
        // line is a hair behind it as far as the depth buffer is concerned.
        slider(context, "label occlusion tolerance", 0, 0.5f, DemoConfig.TERRAIN_OCCLUSION_TOLERANCE, false, new FloatSetting() {
            public void set(float value) {
                DemoConfig.TERRAIN_OCCLUSION_TOLERANCE = value;
                DemoConfig.PEAK_FINDER_OCCLUSION_TOLERANCE = value;
                demo.applyTerrainOptions();
            }
        });
    }

    private static void buildSkyFogSection(Context context, final DemoMap demo) {
        header(context, "SKY");
        check(context, "sky", DemoConfig.SKY_ENABLED, new BoolSetting() {
            public void set(boolean value) { DemoConfig.SKY_ENABLED = value; demo.skyOptions.setEnabled(value); }
        });
        // The scattering sky against the old two-colour gradient - the A/B for both look and cost.
        check(context, "atmosphere (off = gradient)", !"gradient".equals(DemoConfig.SKY_TYPE), new BoolSetting() {
            public void set(boolean value) { DemoConfig.SKY_TYPE = value ? "atmosphere" : "gradient"; demo.applySkyOptions(); }
        });
        slider(context, "atmosphere sun intensity", 0, 30, DemoConfig.SKY_ATMO_SUN, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SKY_ATMO_SUN = value; demo.skyOptions.setAtmosphereSunIntensity(value); }
        });
        slider(context, "atmosphere exposure (lower = brighter)", 0.1f, 4, DemoConfig.SKY_ATMO_LUMINANCE, false, new FloatSetting() {
            public void set(float value) { DemoConfig.SKY_ATMO_LUMINANCE = value; demo.skyOptions.setAtmosphereLuminance(value); }
        });

        // Buildings come from the STYLE, so the switch rebuilds the base layer. Only the inline
        // style is generated here, so it is the one this can turn on and off; a dir/zip/project style
        // draws whatever it was authored with.
        check(context, "3D buildings (inline style)", DemoConfig.INLINE_BUILDINGS_3D, new BoolSetting() {
            public void set(boolean value) { DemoConfig.INLINE_BUILDINGS_3D = value; demo.rebuildBaseLayer(); }
        });

        header(context, "FOG / DISTANCE");
        // The master switch on FogOptions: every value below stays configured while it is off.
        check(context, "fog", DemoConfig.FOG_ENABLED, new BoolSetting() {
            public void set(boolean value) {
                DemoConfig.FOG_ENABLED = value;
                demo.fogOptions.setEnabled(value);
                demo.mapView.requestRender();
            }
        });
        // DIRECT = the sliders below, STYLE = the inline style's Map block (mapbox-shaped, and the
        // only one that can be zoom-dependent). Rebuilds the layer: the fog is IN the stylesheet.
        final String[] fogSources = { DemoConfig.FOG_SOURCE_DIRECT, DemoConfig.FOG_SOURCE_STYLE };
        choice(context, "fog source", fogSources,
               DemoConfig.FOG_SOURCE_STYLE.equals(DemoConfig.FOG_SOURCE) ? 1 : 0, new IntSetting() {
            public void set(int index) {
                DemoConfig.FOG_SOURCE = fogSources[index];
                demo.applyFogOptions();
                demo.rebuildBaseLayer();
            }
        });
        // Named looks. Each writes every fog field, so the sliders below are a starting point the
        // preset resets - they keep their old POSITIONS until the panel is reopened, which is fine
        // for picking a look and then tuning it.
        choice(context, "fog preset", DemoConfig.FOG_PRESETS, 0, new IntSetting() {
            public void set(int index) {
                DemoConfig.applyFogPreset(DemoConfig.FOG_PRESETS[index]);
                DemoConfig.FOG_ENABLED = true;
                demo.applyFogOptions();
            }
        });
        // The range is in multiples of the camera-to-focus distance, so it holds at every zoom.
        slider(context, "fog range start (x camera)", 0, 4, DemoConfig.FOG_RANGE_START, false, new FloatSetting() {
            public void set(float value) { DemoConfig.FOG_RANGE_START = value; demo.fogOptions.setRangeStart(value); }
        });
        slider(context, "fog range end (x camera)", 0, 20, DemoConfig.FOG_RANGE_END, false, new FloatSetting() {
            public void set(float value) { DemoConfig.FOG_RANGE_END = value; demo.fogOptions.setRangeEnd(value); }
        });
        // How far up the SKY the same haze reaches. The ground takes the very same term, so the
        // two meet at the skyline whatever this is set to.
        slider(context, "fog horizon blend (0..1)", 0, 1, DemoConfig.FOG_HORIZON_BLEND, false, new FloatSetting() {
            public void set(float value) { DemoConfig.FOG_HORIZON_BLEND = value; demo.fogOptions.setHorizonBlend(value); }
        });
        // Peaks poking out of a valley haze (mapbox vertical-range). Equal values = off.
        slider(context, "fog vertical start (m)", 0, 4000, DemoConfig.FOG_VERTICAL_START, false, new FloatSetting() {
            public void set(float value) { DemoConfig.FOG_VERTICAL_START = value; demo.fogOptions.setVerticalRangeStart(value); }
        });
        slider(context, "fog vertical end (m)", 0, 4000, DemoConfig.FOG_VERTICAL_END, false, new FloatSetting() {
            public void set(float value) { DemoConfig.FOG_VERTICAL_END = value; demo.fogOptions.setVerticalRangeEnd(value); }
        });
        slider(context, "fog stars (0..1)", 0, 1, DemoConfig.FOG_STAR_INTENSITY, false, new FloatSetting() {
            public void set(float value) { DemoConfig.FOG_STAR_INTENSITY = value; demo.fogOptions.setStarIntensity(value); }
        });
        // Changes the visible tile set, so apply on release only.
        slider(context, "tile LOD (x tangram, 0=finest)", 0, 4, DemoConfig.TILE_LOD_FACTOR, true, new FloatSetting() {
            public void set(float value) { DemoConfig.TILE_LOD_FACTOR = value; demo.mapView.getOptions().setTileLODFactor(value); }
        });
        // The GRAZING half of the same test. Unbounded it swings with the camera, so under a tilt
        // one side of the horizon keeps its detail and the other loses a level; 0 = no limit.
        slider(context, "tile LOD grazing limit (levels, 0=none)", 0, 4, DemoConfig.TILE_LOD_GRAZING, true, new FloatSetting() {
            public void set(float value) { DemoConfig.TILE_LOD_GRAZING = value; demo.mapView.getOptions().setTileLODForeshorteningLimit(value); }
        });
        // Which zoom the vector tiles are FETCHED at, against the view's. -1 is what a style
        // written for MapBox's 512 px tiles wants; it also shifts every `[zoom >= N]` rule a level
        // away from the `view::zoom` ramps beside it, which is what hides a height ramp.
        slider(context, "vector zoom bias (levels)", -2, 2, DemoConfig.VECTOR_ZOOM_BIAS, true, new FloatSetting() {
            public void set(float value) {
                DemoConfig.VECTOR_ZOOM_BIAS = value;
                if (demo.baseLayer != null) {
                    demo.baseLayer.setZoomLevelBias(value);
                }
            }
        });
        slider(context, "tile coarsening (levels)", 0, 6, DemoConfig.TERRAIN_MAX_TILE_ZOOM_COARSENING, true, new FloatSetting() {
            public void set(float value) { DemoConfig.TERRAIN_MAX_TILE_ZOOM_COARSENING = (int) value; demo.terrainOptions.setMaxTileZoomCoarsening((int) value); }
        });
        slider(context, "view distance (x tangram, 0=all)", 0, 4, DemoConfig.VIEW_DISTANCE_FACTOR, true, new FloatSetting() {
            public void set(float value) { DemoConfig.VIEW_DISTANCE_FACTOR = value < 0.05f ? 0 : value; demo.terrainOptions.setViewDistanceFactor(DemoConfig.VIEW_DISTANCE_FACTOR); }
        });
        // Absolute distance only EXTENDS the factor above: the ground reaches at least this far
        // whatever the camera's height and pitch, which is what a view along the ground wants.
        slider(context, "view distance (km, 0=factor)", 0, 300, DemoConfig.VIEW_DISTANCE_METERS / 1000.0f, true, new FloatSetting() {
            public void set(float value) { DemoConfig.VIEW_DISTANCE_METERS = value < 0.5f ? 0 : value * 1000.0f; demo.terrainOptions.setViewDistance(DemoConfig.VIEW_DISTANCE_METERS); }
        });
        slider(context, "auto-flatten (px parallax, 0=off)", 0, 20, DemoConfig.AUTO_FLATTEN_PARALLAX, true, new FloatSetting() {
            public void set(float value) { DemoConfig.AUTO_FLATTEN_PARALLAX = value; demo.terrainOptions.setAutoFlattenParallax(value); }
        });
        slider(context, "auto-flatten tilt (deg, 0=off)", 0, 90, DemoConfig.AUTO_FLATTEN_TILT, true, new FloatSetting() {
            public void set(float value) { DemoConfig.AUTO_FLATTEN_TILT = value; demo.terrainOptions.setAutoFlattenTilt(value); }
        });
    }

    /** One-shot actions: post-process effects and the routing / search / geometry test cases. */
    private static void buildDebugSection(Context context, final DemoMap demo) {
        header(context, "DEBUG");
        check(context, "tile borders", DemoConfig.DEBUG_TILE_BORDERS, new BoolSetting() {
            public void set(boolean value) { DemoConfig.DEBUG_TILE_BORDERS = value; demo.applyDebugConfig(); }
        });
    }

    private static void buildActionsSection(final Context context, final DemoMap demo) {
        header(context, "ACTIONS");
        // The only way to trigger a two-finger gesture without fingers: in free roam 'fps' this
        // is the move, everywhere else the pan.
        button(context, "two-finger drag: forward", new Action() {
            public void run() { DemoTests.runTwoFingerDrag(demo, 0, -500); }
        });
        button(context, "two-finger drag: strafe", new Action() {
            public void run() { DemoTests.runTwoFingerDrag(demo, 400, 0); }
        });
        button(context, "maneuver head: next svg", new Action() {
            public void run() { DemoTests.report(demo, "maneuver head: " + demo.cycleManeuverHead()); }
        });
        button(context, "offline routing test", new Action() {
            public void run() { DemoTests.runOfflineRouting(demo); }
        });
        button(context, "online routing test", new Action() {
            public void run() { DemoTests.runOnlineRouting(demo); }
        });
        button(context, "vector tile search test", new Action() {
            public void run() { DemoTests.runVectorTileSearch(demo); }
        });
        button(context, "geojson line test", new Action() {
            public void run() { DemoTests.addGeoJSONLine(demo); }
        });
        button(context, "popup font sampler", new Action() {
            public void run() { DemoTests.runPopupFonts(demo, "sample"); }
        });
        button(context, "geojson bench: many routes", new Action() {
            public void run() { DemoTests.runGeoJSONBench(demo, "many"); }
        });
        button(context, "geojson bench: long routes", new Action() {
            public void run() { DemoTests.runGeoJSONBench(demo, "long"); }
        });
        button(context, "load bench layer: many routes", new Action() {
            public void run() { DemoTests.addGeoJSONBenchLayer(demo, "many"); }
        });
        button(context, "load bench layer: long routes", new Action() {
            public void run() { DemoTests.addGeoJSONBenchLayer(demo, "long"); }
        });
    }

    // =============================================================================================
    // WIDGET BUILDERS
    // Every one of them registers a filterable row, and lands in the section the last header
    // opened - which is what keeps adding a knob a one-line job.
    // =============================================================================================

    private static int dp(float value) {
        return Math.round(value * density);
    }

    private static GradientDrawable rounded(int color, float radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        return drawable;
    }

    private static GradientDrawable roundedTop(int color, float radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        float r = dp(radiusDp);
        drawable.setCornerRadii(new float[] { r, r, r, r, 0, 0, 0, 0 });
        return drawable;
    }

    /** Starts a new collapsible section. Opening one closes the others. */
    private static void header(Context context, String name) {
        LinearLayout container = new LinearLayout(context);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setBackground(rounded(COLOR_SECTION, 12));

        final TextView title = new TextView(context);
        title.setText(name);
        title.setTextColor(COLOR_TEXT);
        title.setTextSize(13);
        title.setTypeface(null, Typeface.BOLD);
        title.setAllCaps(true);
        title.setPadding(dp(14), dp(12), dp(14), dp(12));
        container.addView(title);

        LinearLayout content = new LinearLayout(context);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(8), 0, dp(8), dp(8));
        content.setVisibility(View.GONE);
        container.addView(content);

        final Section section = new Section(container, title, content, name);
        sectionList.add(section);
        title.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                boolean open = !section.expanded;
                for (Section other : sectionList) {
                    other.expanded = (other == section) && open;
                    expand(other, other.expanded);
                }
            }
        });
        expand(section, false);

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(8);
        sections.addView(container, lp);
        currentContent = content;
    }

    private static void addRow(View view, String text) {
        Section section = sectionList.isEmpty() ? null : sectionList.get(sectionList.size() - 1);
        rows.add(new Row(view, text.toLowerCase(), section));
        currentContent.addView(view, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    private static TextView label(Context context, String value) {
        TextView text = new TextView(context);
        text.setText(value);
        text.setTextSize(11);
        text.setTextColor(COLOR_DIM);
        text.setPadding(dp(6), dp(4), dp(6), dp(4));
        addRow(text, value);
        return text;
    }

    private static CheckBox check(Context context, String label, boolean initial, final BoolSetting setting) {
        CheckBox box = new CheckBox(context);
        box.setText(label);
        box.setTextColor(COLOR_TEXT);
        box.setTextSize(14);
        box.setPadding(dp(8), dp(6), dp(6), dp(6));
        box.setChecked(initial);
        box.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                setting.set(isChecked);
            }
        });
        addRow(box, label);
        return box;
    }

    /**
     * Continuous while dragging unless applyOnRelease: some settings (mesh/drape resolution,
     * contour generation) throw away every cached tile when they change, so applying them per
     * pixel of drag is a guaranteed stall.
     */
    private static void slider(Context context, final String label,
                               float min, float max, float initial,
                               final boolean applyOnRelease, final FloatSetting setting) {
        final float lo = min, span = max - min;
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(dp(8), dp(4), dp(8), dp(2));

        final TextView text = new TextView(context);
        text.setTextColor(COLOR_DIM);
        text.setTextSize(12);
        text.setText(String.format("%s  %.2f%s", label, initial, applyOnRelease ? "  (release)" : ""));
        row.addView(text);

        SeekBar seek = new SeekBar(context);
        seek.setMax(1000);
        seek.setProgress((int) ((initial - lo) / span * 1000));
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            private float valueOf(SeekBar bar) { return lo + span * bar.getProgress() / 1000.0f; }
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                text.setText(String.format("%s  %.2f%s", label, valueOf(seekBar), applyOnRelease ? "  (release)" : ""));
                if (!applyOnRelease) {
                    setting.set(valueOf(seekBar));
                }
            }
            public void onStartTrackingTouch(SeekBar seekBar) { }
            public void onStopTrackingTouch(SeekBar seekBar) {
                setting.set(valueOf(seekBar));
            }
        });
        row.addView(seek, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        addRow(row, label);
    }

    /** A row of small buttons acting as a radio group (no spinner: less code, easier to read). */
    private static void choice(Context context, String label,
                               final String[] options, int initial, final IntSetting setting) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(dp(8), dp(4), dp(8), dp(4));

        TextView text = new TextView(context);
        text.setText(label);
        text.setTextColor(COLOR_DIM);
        text.setTextSize(12);
        row.addView(text);

        final LinearLayout buttonRow = new LinearLayout(context);
        buttonRow.setOrientation(LinearLayout.HORIZONTAL);
        final Button[] buttons = new Button[options.length];
        for (int i = 0; i < options.length; i++) {
            final int index = i;
            Button button = new Button(context);
            button.setText(options[i]);
            button.setTextSize(11);
            button.setAllCaps(false);
            button.setPadding(dp(2), 0, dp(2), 0);
            button.setBackground(rounded(0x33FFFFFF, 8));
            button.setTextColor(COLOR_TEXT);
            button.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    setting.set(index);
                    highlight(buttons, index);
                }
            });
            buttons[i] = button;
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(36), 1f);
            lp.rightMargin = dp(4);
            buttonRow.addView(button, lp);
        }
        highlight(buttons, initial);
        row.addView(buttonRow);
        addRow(row, label + " " + join(options));
    }

    private static String join(String[] values) {
        StringBuilder builder = new StringBuilder();
        for (String value : values) {
            builder.append(value).append(' ');
        }
        return builder.toString();
    }

    private static void highlight(Button[] buttons, int selected) {
        for (int i = 0; i < buttons.length; i++) {
            buttons[i].setTypeface(null, i == selected ? Typeface.BOLD : Typeface.NORMAL);
            buttons[i].setAlpha(i == selected ? 1f : 0.5f);
            buttons[i].setBackground(rounded(i == selected ? 0x5535D6C0 : 0x33FFFFFF, 8));
        }
    }

    private static void button(Context context, String label, final Action action) {
        Button button = new Button(context);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextSize(13);
        button.setTextColor(COLOR_TEXT);
        button.setBackground(rounded(0x33FFFFFF, 10));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(42));
        lp.topMargin = dp(4);
        lp.leftMargin = dp(8);
        lp.rightMargin = dp(8);
        button.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { action.run(); }
        });
        Section section = sectionList.isEmpty() ? null : sectionList.get(sectionList.size() - 1);
        rows.add(new Row(button, label.toLowerCase(), section));
        currentContent.addView(button, lp);
    }

    private static String[] enumNames(Enum<?>[] values) {
        String[] names = new String[values.length];
        for (int i = 0; i < values.length; i++) {
            names[i] = values[i].name().toLowerCase();
        }
        return names;
    }

    private DemoPanel() {
    }
}
