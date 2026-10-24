package com.massifmaps.MassifDemo.demo;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.util.Log;

import com.massifmaps.ui.MapView;

/**
 * Points the camera where the device points: turning the phone turns the view, raising it looks up.
 *
 * This is the star-sky demo's reason for a negative tilt. The map's rotation is the opposite of the
 * heading (rotating the map right turns the view left), and the elevation above the horizon IS the
 * negative tilt the SDK now supports - at tilt -90 the view looks straight at the zenith.
 *
 * A rotation-vector sensor is fused and already smooth; the extra low pass here only takes the last
 * of the jitter out, and an update below the threshold is dropped so a still phone does not
 * re-render for ever.
 */
public final class DemoOrientation implements SensorEventListener {

    private static final String TAG = "DemoOrientation";
    /** Smoothing of the fused reading: 1 = raw, smaller = calmer and laggier. */
    private static final float SMOOTHING = 0.2f;
    /** Below this much movement, in degrees, the camera is left alone. */
    private static final float DEAD_ZONE_DEGREES = 0.3f;

    private final Context context;
    private final MapView mapView;
    private final float[] rotationMatrix = new float[9];
    private final float[] remapped = new float[9];
    private final float[] orientation = new float[3];

    private SensorManager sensorManager;
    private boolean running;
    private boolean primed;
    private float heading;
    private float elevation;

    public DemoOrientation(Context context, MapView mapView) {
        this.context = context;
        this.mapView = mapView;
    }

    public boolean isRunning() {
        return running;
    }

    public void start() {
        if (running) {
            return;
        }
        sensorManager = (SensorManager) context.getSystemService(Context.SENSOR_SERVICE);
        Sensor sensor = sensorManager != null ? sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR) : null;
        if (sensor == null) {
            Log.w(TAG, "no rotation vector sensor: orientation following is not available");
            return;
        }
        sensorManager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME);
        running = true;
        primed = false;
    }

    public void stop() {
        if (!running) {
            return;
        }
        sensorManager.unregisterListener(this);
        running = false;
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values);
        // The phone is held up like a window on the sky, so the axis pointing OUT of the back of
        // the device is what aims the camera: remapping X/Z gives an orientation whose pitch is
        // measured from the horizon, 0 looking level and -90 looking at the zenith.
        SensorManager.remapCoordinateSystem(rotationMatrix, SensorManager.AXIS_X, SensorManager.AXIS_Z, remapped);
        SensorManager.getOrientation(remapped, orientation);

        float newHeading = (float) Math.toDegrees(orientation[0]);
        // pitch: 0 level, negative looking up. A negative map tilt is exactly the same thing.
        float newElevation = (float) Math.toDegrees(orientation[1]);

        if (!primed) {
            heading = newHeading;
            elevation = newElevation;
            primed = true;
        } else {
            // Headings wrap: smooth the SHORTEST way round, or crossing north swings the view
            // through a full turn.
            float delta = ((newHeading - heading + 540f) % 360f) - 180f;
            heading += SMOOTHING * delta;
            elevation += SMOOTHING * (newElevation - elevation);
        }

        float tilt = Math.max(-DemoConfig.LOOK_UP_LIMIT, Math.min(90f, elevation));
        float rotation = -heading;
        if (Math.abs(tilt - ObjectApi.view(mapView).getTilt()) < DEAD_ZONE_DEGREES
                && Math.abs(((rotation - ObjectApi.view(mapView).getRotation() + 540f) % 360f) - 180f) < DEAD_ZONE_DEGREES) {
            return;
        }
        ObjectApi.view(mapView).setTilt(tilt, 0);
        ObjectApi.view(mapView).setRotation(rotation, 0);
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }
}
