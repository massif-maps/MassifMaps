package com.massifmaps.MassifDemo;

import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;

import com.google.android.material.button.MaterialButton;
import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleLive;
import com.massifmaps.MassifDemo.examples.Examples;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.api.MassifMap;
import com.massifmaps.components.PanningMode;
import com.massifmaps.core.MapPos;
import com.massifmaps.projections.EPSG4326;
import com.massifmaps.ui.MapView;

/**
 * Runs one example.
 *
 * Owns every piece of Android an example would otherwise have to write: the view, the back and
 * source buttons, the control row, the caption. The example itself only ever sees
 * {@link ExampleHost}, which is what keeps its source readable as documentation.
 *
 *   adb shell am start -n com.massifmaps.MassifDemo/.ExampleActivity --es example markers
 */
public class ExampleActivity extends AppCompatActivity implements ExampleHost {

    public static final String EXTRA_ID = "example";

    private static final String TAG = "ExampleActivity";

    private final Handler ui = new Handler(Looper.getMainLooper());

    private MapView mapView;
    private MassifMap map;
    private MapExample example;
    private Examples.Entry entry;
    private LinearLayout controls;
    private TextView caption;
    private View busy;
    private boolean chromeHidden;
    /** The CONFIG broadcast, so a knob can be changed on the running example (see ExampleLive). */
    private ExampleLive live;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        setContentView(R.layout.example_activity);

        entry = Examples.byId(getIntent().getStringExtra(EXTRA_ID));
        if (entry == null) {
            Toast.makeText(this, "No example '" + getIntent().getStringExtra(EXTRA_ID) + "'",
                           Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        // The SDK's own log, on: a spec key the class does not have is a WARNING, and with the
        // log off it looks like the property silently did nothing.
        com.massifmaps.utils.Log.setShowInfo(true);
        com.massifmaps.utils.Log.setShowWarn(true);
        com.massifmaps.utils.Log.setShowError(true);

        mapView = findViewById(R.id.mapView);
        controls = findViewById(R.id.controls);
        caption = findViewById(R.id.caption);
        busy = findViewById(R.id.busy);
        ((TextView) findViewById(R.id.exampleTitle)).setText(entry.title());
        findViewById(R.id.backButton).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });
        findViewById(R.id.codeButton).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startActivity(new Intent(ExampleActivity.this, CodeActivity.class)
                                  .putExtra(EXTRA_ID, entry.id()));
            }
        });

        // Lon/lat everywhere. Every example is written in degrees, and an example that had to
        // convert would be teaching the wrong thing.
        mapView.getOptions().setBaseProjection(new EPSG4326());
        mapView.getOptions().setRestrictedPanning(true);
        mapView.getOptions().setSeamlessPanning(true);
        mapView.getOptions().setPanningMode(PanningMode.PANNING_MODE_STICKY);
        mapView.getOptions().setRotatable(true);
        // What every other map app does and nobody thinks about: double-tap-and-drag to zoom, and
        // a two-finger tap to zoom out. Both are one flag.
        mapView.getOptions().setZoomGestures(true);
        mapView.getOptions().setClickTypeDetection(true);

        map = MassifMap.attach(mapView, entry.id()).eventProjection("EPSG:4326");
        // '--es ui false' strips the chrome, the same key the bench uses. A gallery vignette is a
        // picture of the MAP; the back button and the caption are the app around it.
        if ("false".equals(getIntent().getStringExtra("ui"))) {
            findViewById(R.id.exampleBar).setVisibility(View.GONE);
            findViewById(R.id.controlScroll).setVisibility(View.GONE);
            caption.setVisibility(View.GONE);
            chromeHidden = true;
        }
        applyInsets();
        // The same channel and the same adb keys the bench answers to, so an example can be tuned
        // without a relaunch - which is the only way to see whether one change reaches every pass
        // in the same frame.
        live = new ExampleLive(map);
        ContextCompat.registerReceiver(this, live, new IntentFilter(ExampleLive.ACTION),
                                       ContextCompat.RECEIVER_NOT_EXPORTED);
        start();
    }

    /**
     * Off the UI thread, like the bench: building a layer decodes its style, and a real style
     * project takes seconds. The example's own controls come back through the host, which posts.
     */
    private void start() {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    example = entry.create();
                    example.onStart(ExampleActivity.this);
                } catch (Exception e) {
                    Log.e(TAG, "example '" + entry.id() + "' failed", e);
                    toast("Failed: " + e.getMessage());
                }
                applyCameraOverrides();
                logCamera();
                ui.post(new Runnable() {
                    @Override
                    public void run() {
                        busy.setVisibility(View.GONE);
                    }
                });
            }
        }, "example-" + entry.id()).start();
    }

    @Override
    protected void onDestroy() {
        ui.removeCallbacksAndMessages(null);
        if (live != null) {
            unregisterReceiver(live);
            live = null;
        }
        if (example != null) {
            example.onStop();
        }
        // Drops the map's id, its handlers and every layer it built - so leaving an example and
        // opening another cannot collide on an id, and nothing is left registered.
        if (map != null) {
            map.close();
        }
        super.onDestroy();
    }


    /**
     * Camera overrides from the intent, applied AFTER the example has set its own.
     *
     * Composing a screenshot is a loop - nudge the focus, look, nudge again - and a rebuild per
     * nudge makes it unaffordable:
     *
     *   am start -n .../.ExampleActivity --es example terrain-3d \
     *       --es lat 45.974 --es zoom 12.9 --es tilt 27 --es rotation 180
     *
     * Once it looks right, the numbers go back into the example's own moveTo.
     */
    private void applyCameraOverrides() {
        Bundle extras = getIntent() != null ? getIntent().getExtras() : null;
        if (extras == null) {
            return;
        }
        MapPos focus = map.camera().position();
        double lon = number(extras, "lon", focus.getX());
        double lat = number(extras, "lat", focus.getY());
        float zoom = (float) number(extras, "zoom", map.camera().zoom());
        float rotation = (float) number(extras, "rotation", map.camera().rotation());
        float tilt = (float) number(extras, "tilt", map.camera().tilt());
        if (extras.containsKey("lon") || extras.containsKey("lat") || extras.containsKey("zoom")
                || extras.containsKey("rotation") || extras.containsKey("tilt")) {
            map.camera().moveTo(new MapPos(lon, lat), zoom, rotation, tilt);
        }
        logCamera();
    }

    /**
     * Where the camera ACTUALLY ended up, always.
     *
     * A screenshot of the wrong place looks like a rendering bug and is not one - a dark frame
     * that read as "the shadows are broken" turned out to be the open Atlantic. Read it back
     * rather than trusting what was asked for.
     */
    private void logCamera() {
        MapPos at = map.camera().position();
        Log.i(TAG, String.format("camera lon=%.5f lat=%.5f zoom=%.2f rotation=%.0f tilt=%.0f",
                                 at.getX(), at.getY(), map.camera().zoom(),
                                 map.camera().rotation(), map.camera().tilt()));
    }

    /** Intent extras arrive as strings (--es), so a number has to be parsed rather than read. */
    private static double number(Bundle extras, String key, double fallback) {
        String value = extras.getString(key);
        try {
            return value != null ? Double.parseDouble(value) : fallback;
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    // --- ExampleHost -------------------------------------------------------------------------

    @Override
    public MassifMap map() {
        return map;
    }

    @Override
    public Context context() {
        return this;
    }

    @Override
    public void caption(final String text) {
        ui.post(new Runnable() {
            @Override
            public void run() {
                if (chromeHidden) {
                    return;
                }
                caption.setText(text == null ? "" : text);
                caption.setVisibility(text == null || text.isEmpty() ? View.GONE : View.VISIBLE);
            }
        });
    }

    @Override
    public void button(final String label, final Runnable action) {
        ui.post(new Runnable() {
            @Override
            public void run() {
                if (chromeHidden) {
                    return;
                }
                MaterialButton button = new MaterialButton(ExampleActivity.this, null,
                    com.google.android.material.R.attr.materialButtonOutlinedStyle);
                button.setText(label);
                button.setBackgroundColor(0xF2FFFFFF);
                button.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View v) {
                        action.run();
                    }
                });
                controls.addView(button, controlParams());
            }
        });
    }

    @Override
    public void toggle(final String label, final boolean on, final OnToggle action) {
        ui.post(new Runnable() {
            @Override
            public void run() {
                if (chromeHidden) {
                    return;
                }
                final MaterialButton button = new MaterialButton(ExampleActivity.this, null,
                    com.google.android.material.R.attr.materialButtonOutlinedStyle);
                button.setText(label);
                button.setTag(on);
                paint(button, on);
                button.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View v) {
                        boolean next = !((Boolean) button.getTag());
                        button.setTag(next);
                        paint(button, next);
                        action.onToggle(next);
                    }
                });
                controls.addView(button, controlParams());
            }
        });
    }

    @Override
    public void toast(final String text) {
        ui.post(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(ExampleActivity.this, text, Toast.LENGTH_SHORT).show();
            }
        });
    }

    @Override
    public void postDelayed(Runnable action, long millis) {
        ui.postDelayed(action, millis);
    }

    // --- chrome ------------------------------------------------------------------------------

    /** An on button is filled with the theme's primary; an off one is a plain white pill. */
    private void paint(MaterialButton button, boolean on) {
        button.setBackgroundColor(on ? ContextCompat.getColor(this, R.color.massif_primary)
                                    : 0xF2FFFFFF);
        button.setTextColor(on ? 0xFFFFFFFF
                               : ContextCompat.getColor(this, R.color.massif_on_surface));
    }

    private static LinearLayout.LayoutParams controlParams() {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.rightMargin = 8;
        return params;
    }

    private void applyInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.exampleRoot),
            new androidx.core.view.OnApplyWindowInsetsListener() {
                @Override
                public WindowInsetsCompat onApplyWindowInsets(View view, WindowInsetsCompat insets) {
                    Insets bars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
                    findViewById(R.id.exampleBar).setPadding(4, bars.top + 4, 8, 4);
                    caption.setPadding(16, 10, 16, 10 + bars.bottom);
                    return insets;
                }
            });
    }
}
