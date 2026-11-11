package com.massifmaps.MassifDemo.examples.styles;

import android.app.Activity;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.constraintlayout.widget.ConstraintLayout;

import com.massifmaps.MassifDemo.R;
import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

@ExampleInfo(
    id = "map-legend",
    title = "Map legend",
    description = "A legend resolved from the live style: switch the Massif variant and every swatch "
                + "follows, because none of its colours is written down anywhere but in the style.",
    section = Sections.STYLES,
    order = 40)
public class MapLegendExample extends MapExample {

    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    private static final String[][] VARIANTS = {
        { "streets", "Streets" }, { "outdoor", "Outdoor" }, { "topo", "Topo" }, { "eink", "E-ink" },
    };

    private Activity activity;
    private LinearLayout list;
    private float density;

    @Override
    public void onStart(final ExampleHost host) {
        MassifMap map = host.map();
        activity = (Activity) host.context();
        density = activity.getResources().getDisplayMetrics().density;

        final MassifObject style = map.style("massif", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "outdoor")));
        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "massif"));
        map.camera().moveTo(new Position(5.7262, 45.2), 13f);

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                addPanel();
                showLegend(style);
            }
        });

        for (final String[] variant : VARIANTS) {
            host.button(variant[1], new Runnable() {
                @Override
                public void run() {
                    style.set("params.variant", variant[0]);
                    showLegend(style);
                    host.caption("Massif " + variant[1] + ": the legend was resolved again from the style.");
                }
            });
        }
        host.caption("Massif Outdoor, and its legend: colours read from the compiled style.");
    }

    private void addPanel() {
        ConstraintLayout root = activity.findViewById(R.id.exampleRoot);
        list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(dp(10), dp(4), dp(10), dp(4));
        ScrollView panel = new ScrollView(activity);
        panel.setBackgroundColor(0xEEFFFFFF);
        panel.addView(list);
        ConstraintLayout.LayoutParams params = new ConstraintLayout.LayoutParams(dp(230), 0);
        params.topToBottom = R.id.exampleBar;
        params.bottomToTop = R.id.sliders;
        params.endToEnd = ConstraintLayout.LayoutParams.PARENT_ID;
        params.setMargins(0, dp(8), dp(8), dp(8));
        root.addView(panel, params);
    }

    /** getLegend reads the decoder's CURRENT parameters: call it again after any change. */
    private void showLegend(MassifObject style) {
        list.removeAllViews();
        MassifObject result = style.call("getLegend", "");
        try {
            JSONArray sections = new JSONObject(result.json()).getJSONArray("sections");
            for (int i = 0; i < sections.length(); i++) {
                JSONObject section = sections.getJSONObject(i);
                TextView heading = text(section.opt("label"), 13);
                heading.setTypeface(Typeface.DEFAULT_BOLD);
                heading.setPadding(0, dp(10), 0, dp(4));
                list.addView(heading);
                JSONArray items = section.getJSONArray("items");
                for (int j = 0; j < items.length(); j++) {
                    list.addView(row(items.getJSONObject(j)));
                }
            }
        } catch (JSONException e) {
            list.addView(text("No legend: " + e.getMessage(), 12));
        } finally {
            result.close();
        }
    }

    private View row(JSONObject item) {
        LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        View swatch = new View(activity);
        swatch.setBackgroundColor(colorOf(item));
        row.addView(swatch, new LinearLayout.LayoutParams(dp(44), dp(16)));
        TextView label = text(item.opt("label"), 12);
        label.setPadding(dp(8), dp(3), 0, dp(3));
        row.addView(label);
        return row;
    }

    /** The top-most colour the item draws with, whatever its kind. */
    private static int colorOf(JSONObject item) {
        JSONArray lines = item.optJSONArray("lines");
        JSONObject top = lines != null && lines.length() > 0 ? lines.optJSONObject(lines.length() - 1) : item;
        String css = top.optString("color", "");
        for (String key : new String[] { "plate", "marker", "text" }) {
            JSONObject nested = item.optJSONObject(key);
            if (css.isEmpty() && nested != null) {
                css = nested.optString("color", "");
            }
        }
        try {
            int alpha = css.length() == 9 ? Integer.parseInt(css.substring(7, 9), 16) : 255;
            return alpha << 24 | Integer.parseInt(css.substring(1, 7), 16);
        } catch (RuntimeException e) {
            return 0x00000000;
        }
    }

    /** A label is a string or a {lang: text} object. */
    private TextView text(Object label, int size) {
        String value = label == null ? "" : label.toString();
        if (label instanceof JSONObject) {
            JSONObject names = (JSONObject) label;
            value = names.optString("en", names.length() > 0 ? names.optString(names.keys().next(), "") : "");
        }
        TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(0xFF000000);
        return view;
    }

    private int dp(int value) {
        return Math.round(value * density);
    }
}
