package com.massifmaps.MassifDemo;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.WindowCompat;

import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import com.massifmaps.MassifDemo.demo.DemoLive;

import com.massifmaps.MassifDemo.ui.main.SecondFragment;

/**
 * The BENCH: one composable map with every knob on it, used for debugging and measurement.
 *
 * This is NOT an example. It is the screen the root CLAUDE.md documents - layer switches, intent
 * extras, the live-config broadcast, the scripted animations - and it is deliberately separate
 * from the gallery so the two do not constrain each other.
 *
 *   adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity --es ui false --es hs true
 */
public class BenchActivity extends AppCompatActivity {
    private final String TAG = "BenchActivity";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.d(TAG, "onCreate");
        // Draw edge to edge: the map runs under the status and navigation bars, and the overlay
        // (DemoPanel) insets itself so nothing lands under them.
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        setContentView(R.layout.main_activity);
        if (savedInstanceState == null) {
            getSupportFragmentManager().beginTransaction()
                    .replace(R.id.container, SecondFragment.newInstance())
                    .commitNow();
        }
    }

    /**
     * 'am start' with extras on the RUNNING demo applies them live instead of relaunching. The
     * activity is singleTop, so Android delivers the intent here rather than recreating it, and the
     * extras take the same path as the CONFIG broadcast (see DemoLive) - a relaunch rebuilds every
     * cache, which is exactly what hides a stale-redraw bug.
     */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        Bundle extras = intent != null ? intent.getExtras() : null;
        if (extras == null || extras.isEmpty()) {
            return;
        }
        Log.d(TAG, "onNewIntent " + extras);
        sendBroadcast(new Intent(DemoLive.ACTION).setPackage(getPackageName()).putExtras(extras));
    }

    @Override
    protected void onPostResume() {
        Log.d(TAG, "onPostResume");
        super.onPostResume();
    }

    @Override
    protected void onStart() {
        Log.d(TAG, "onStart");
        super.onStart();
    }

    @Override
    protected void onStop() {
        Log.d(TAG, "onStop");
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        Log.d(TAG, "onDestroy");
        super.onDestroy();
    }
}
