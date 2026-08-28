package com.massifmaps.MassifDemo.ui.main;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import androidx.fragment.app.Fragment;

import com.massifmaps.MassifDemo.R;
import com.massifmaps.MassifDemo.demo.DemoCfg;
import com.massifmaps.MassifDemo.demo.DemoConfig;
import com.massifmaps.MassifDemo.demo.DemoLive;
import com.massifmaps.MassifDemo.demo.DemoMap;
import com.massifmaps.MassifDemo.demo.DemoPanel;
import com.massifmaps.components.Options;
import com.massifmaps.components.PanningMode;
import com.massifmaps.core.MapPos;
import com.massifmaps.core.MapRange;
import com.massifmaps.projections.EPSG4326;
import com.massifmaps.ui.MapClickInfo;
import com.massifmaps.ui.MapEventListener;
import com.massifmaps.ui.MapView;

import java.io.File;
import java.nio.file.Paths;

/**
 * The demo screen. Deliberately thin: it owns the Android side (view, permissions, map event
 * listener) and nothing else.
 *
 * WHAT THE DEMO SHOWS lives in three places:
 *   - com.massifmaps.MassifDemo.demo.DemoConfig : all defaults, one static field per knob;
 *   - com.massifmaps.MassifDemo.demo.DemoMap    : builds the map from that config, applies changes;
 *   - com.massifmaps.MassifDemo.demo.DemoPanel  : the on-screen panel that edits the config live.
 *
 * There are no separate "examples" any more: every layer (base map, hillshade, contours,
 * satellite, hypsometric tint, test elements, offline routes) is an independent switch, and the
 * base map's style source (dir / zip / inline / project) and mode (plain / composite) are two more.
 */
public class SecondFragment extends Fragment {

    private static final String TAG = "SecondFragment";

    private static final int REQUEST_PERMISSIONS_CODE_WRITE_STORAGE = 1435;
    private static final int REQUEST_PERMISSIONS_MANAGE_STORAGE = 1436;

    public static SecondFragment newInstance() {
        return new SecondFragment();
    }

    private MapView mapView;
    private DemoMap demo;
    private android.content.BroadcastReceiver liveConfigReceiver;
    private TextView zoomText;

    @RequiresApi(api = Build.VERSION_CODES.M)
    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.second_fragment, container, false);

        com.massifmaps.utils.Log.setShowInfo(true);
        com.massifmaps.utils.Log.setShowDebug(true);
        com.massifmaps.utils.Log.setShowWarn(true);
        com.massifmaps.utils.Log.setShowError(true);

        mapView = (MapView) view.findViewById(R.id.mapView);
        zoomText = (TextView) view.findViewById(R.id.zoomText);

        // Base map options that are not part of the demo configuration itself.
        final Options options = mapView.getOptions();
        options.setBaseProjection(new EPSG4326());
        options.setZoomGestures(true);
        options.setRestrictedPanning(true);
        options.setSeamlessPanning(true);
        options.setRotatable(true);
        options.setTiltRange(new MapRange(30, 90));
        options.setPanningMode(PanningMode.PANNING_MODE_STICKY);

        // Intent extras override the DemoConfig defaults; read them before anything is built.
        DemoCfg.attach(getActivity() != null ? getActivity().getIntent() : null);

        // MapView sets RENDERMODE_WHEN_DIRTY: every frame is a request from the native side, which
        // costs a wakeup handshake per frame. '--es continuousRender true' drives the GL thread
        // continuously instead, which is what tells whether that handshake is what paces the map.
        if (DemoConfig.CONTINUOUS_RENDER) {
            mapView.setRenderMode(android.opengl.GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        }
        DemoConfig.applyIntentOverrides();

        checkStoragePermission(view);
        return view;
    }

    /** Builds the whole demo once the data directory is reachable. */
    @RequiresApi(api = Build.VERSION_CODES.O)
    void startDemo(View view) {
        if (demo != null || view == null) {
            return; // permission flow can call back more than once
        }
        String dataPath = resolveDataPath();
        Log.i(TAG, "data path: " + dataPath);

        demo = new DemoMap(getContext(), mapView, dataPath);

        // Off the UI thread: build() decodes the style, and a real style project takes seconds
        // (measured 6.5 s for the bundled assets style on a mid-range device). On the UI thread
        // that is a frozen app and an "isn't responding" dialog before the map ever appears.
        final View demoView = view;
        new Thread(new Runnable() {
            @Override
            public void run() {
                demo.build();
                demoView.post(new Runnable() {
                    @Override
                    public void run() {
                        DemoPanel.build(getContext(), demoView.findViewById(R.id.main), demo);
                        installMapListener();
                        installLiveConfigReceiver();
                        // Re-apply the start camera once the map view has a size: build() sets it
                        // while the view can still be 0x0, and restricted panning then clamps the
                        // focus latitude to 0 - the map opens on the equator with only the
                        // longitude kept.
                        demo.applyCamera();
                    }
                });
            }
        }, "demo-build").start();
    }

    /**
     * Lets adb change any config key on the RUNNING demo (see DemoLive):
     *   adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es fog false
     * Exported on purpose - it is a debug bench, and an unexported receiver cannot be reached
     * from the shell at all.
     */
    private void installLiveConfigReceiver() {
        if (liveConfigReceiver != null || getContext() == null) {
            return;
        }
        liveConfigReceiver = new DemoLive(demo);
        IntentFilter filter = new IntentFilter(DemoLive.ACTION);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            getContext().registerReceiver(liveConfigReceiver, filter, Context.RECEIVER_EXPORTED);
        } else {
            getContext().registerReceiver(liveConfigReceiver, filter);
        }
    }

    @Override
    public void onDestroyView() {
        if (liveConfigReceiver != null && getContext() != null) {
            getContext().unregisterReceiver(liveConfigReceiver);
            liveConfigReceiver = null;
        }
        super.onDestroyView();
    }

    /**
     * The data root: <sd-card>/alpimaps_mbtiles, i.e. four levels above the app's own external
     * files dir, so the same files can be shared with the other test apps.
     */
    private String resolveDataPath() {
        File externalPath = null;
        File[] externalPaths = getContext().getExternalFilesDirs(null);
        if (externalPaths != null && externalPaths.length > 1) {
            externalPath = externalPaths[externalPaths.length - 1];
        }
        if (externalPath == null) {
            externalPath = getContext().getExternalFilesDir(null);
        }
        return Paths.get(externalPath.getAbsolutePath(), "../../../../" + DemoConfig.DATA_DIR_NAME).normalize().toString();
    }

    /** Camera readout (also used by scripted runs through logcat) + terrain-aware click probe. */
    private void installMapListener() {
        mapView.setMapEventListener(new MapEventListener() {
            @Override
            public void onMapMoved(int reason) {
                super.onMapMoved(reason);
                Log.d(TAG, String.format("lat=%.6f lng=%.6f rotation=%.2f z=%.2f tilt=%.0f",
                        mapView.getFocusPos().getY(), mapView.getFocusPos().getX(),
                        mapView.getMapRotation(), mapView.getZoom(), mapView.getTilt()));
                if (getActivity() == null) {
                    return;
                }
                getActivity().runOnUiThread(new Runnable() {
                    public void run() {
                        MapPos focus = mapView.getFocusPos();
                        // mb= is the MapBox zoom the same view is at: their tiles are 512 px wide
                        // and this SDK's are 256, so one level apart. Without it a side-by-side
                        // against mbref.html compares two different zooms.
                        String light = DemoConfig.LIGHT_PRESET.isEmpty() ? "-" : DemoConfig.LIGHT_PRESET;
                        String text = String.format("z=%.2f (mb %.2f)  tilt=%.0f  %s  %.5f, %.5f",
                                mapView.getZoom(), mapView.getZoom() - 1, mapView.getTilt(), light,
                                focus.getY(), focus.getX());
                        if (zoomText != null) {
                            zoomText.setText(text);
                        }
                        if (DemoPanel.statusText != null) {
                            DemoPanel.statusText.setText(text);
                        }
                    }
                });
            }

            @Override
            public void onMapClicked(MapClickInfo mapClickInfo) {
                super.onMapClicked(mapClickInfo);
                // clickPos already resolves to the TERRAIN surface; the elevation query itself may
                // block on tile loading, so it runs off the UI thread.
                final MapPos wgs84Pos = mapView.getOptions().getBaseProjection().toWgs84(mapClickInfo.getClickPos());
                new Thread(new Runnable() {
                    public void run() {
                        final double elevation = demo.getElevation(wgs84Pos);
                        if (getActivity() == null) {
                            return;
                        }
                        getActivity().runOnUiThread(new Runnable() {
                            public void run() {
                                Toast.makeText(getContext(),
                                        String.format("%.5f, %.5f: %.0f m", wgs84Pos.getY(), wgs84Pos.getX(), elevation),
                                        Toast.LENGTH_SHORT).show();
                            }
                        });
                    }
                }).start();
            }
        });
    }

    // =============================================================================================
    // STORAGE PERMISSION (the data files live outside the app's own directory)
    // =============================================================================================

    @SuppressLint("NewApi")
    public void checkStoragePermission(View view) {
        if (getActivity().checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_DENIED) {
            requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_PERMISSIONS_CODE_WRITE_STORAGE);
        } else {
            startDemo(view);
        }
    }

    @RequiresApi(api = Build.VERSION_CODES.R)
    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        // API 30+: WRITE_EXTERNAL_STORAGE is not enough for a shared folder, the app also needs
        // "all files access", which only the system settings screen can grant.
        if (Environment.isExternalStorageManager()) {
            startDemo(getView());
        } else {
            Intent intent = new Intent();
            intent.setAction(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.fromParts("package", getActivity().getPackageName(), null));
            startActivityForResult(intent, REQUEST_PERMISSIONS_MANAGE_STORAGE);
        }
    }

    @RequiresApi(api = Build.VERSION_CODES.O)
    @Override
    public void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_PERMISSIONS_MANAGE_STORAGE && getView() != null) {
            startDemo(getView());
        }
    }
}
