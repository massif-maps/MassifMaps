package com.massifmaps.MassifDemo.customlayer;

import com.massifmaps.api.Massif;
import com.massifmaps.api.MassifLayer;

/**
 * The custom layers of src/main/cpp/custom_layer_demo.c: a heatmap legend in screen space and a
 * heatmap on the ground, both drawn by C over the SDK's C ABI (mm_layer_create_custom). This is
 * the Java end: it registers them and hands the heatmap its grid.
 */
public final class CustomHeatmap {

    /** The grid is N x N cells over a square this many metres across, centred on the start. */
    public static final int N = 160;
    public static final double SIDE_METERS = 8000;
    private static final double MERCATOR_RADIUS = 6378137;

    static {
        System.loadLibrary("massifdemo");
    }

    private static native int nativeCreate(String id, boolean scene);

    private static native void nativeSetHeatmap(double[] xyz, float[] values, int n);

    private static native void nativeSetDrape(boolean drape);

    /**
     * The legend (scene = false) or the heatmap (scene = true), registered under an id. Add it to
     * a map like any other layer. Null when it did not register.
     */
    public static MassifLayer create(String id, boolean scene) {
        return nativeCreate(id, scene) == 0 ? null : Massif.layer(id);
    }

    /**
     * Draped (the default): the heatmap is pulled forward of the ground in depth, so it lies on it
     * between its grid nodes. Off, it only gets the layer's own depth slot and the ground's mesh
     * cuts through it where the relief is steep. Either way a ridge in front hides it.
     */
    public static void setDrape(boolean drape) {
        nativeSetDrape(drape);
    }

    /** Longitude and latitude of the N x N grid nodes, row by row, flat: lon, lat, lon, lat... */
    public static double[] gridPositions(double lon, double lat) {
        double cosLat = Math.cos(Math.toRadians(lat));
        double step = SIDE_METERS / (N - 1);
        double[] positions = new double[2 * N * N];
        for (int j = 0; j < N; j++) {
            for (int i = 0; i < N; i++) {
                double east = (i - (N - 1) / 2.0) * step, north = (j - (N - 1) / 2.0) * step;
                positions[2 * (j * N + i)] = lon + east / (111320 * cosLat);
                positions[2 * (j * N + i) + 1] = lat + north / 110540;
            }
        }
        return positions;
    }

    /**
     * Hands the heatmap its grid: the positions of {@link #gridPositions} with the ground height
     * at each, in metres (as drawn, so already exaggerated), and puts a few hot spots on it.
     * NaN or missing heights count as sea level.
     */
    public static void setHeatmap(double lon, double lat, double[] heights) {
        // Mercator metres whatever the base projection: the SDK's world units are that plane.
        double cosLat = Math.cos(Math.toRadians(lat));
        double[] positions = gridPositions(lon, lat);
        double[][] spots = { { 0, 0, 0.35, 1.0 }, { 0.5, -0.4, 0.2, 0.8 }, { -0.55, 0.45, 0.25, 0.7 } };
        double[] xyz = new double[3 * N * N];
        float[] values = new float[N * N];
        for (int k = 0; k < N * N; k++) {
            double height = heights != null && k < heights.length && !Double.isNaN(heights[k]) && heights[k] > -100000 ? heights[k] : 0;
            // 20 m of clearance over the mesh's chords, then the mercator stretch heights carry.
            double lonRad = Math.toRadians(positions[2 * k]), latRad = Math.toRadians(positions[2 * k + 1]);
            xyz[3 * k] = MERCATOR_RADIUS * lonRad;
            xyz[3 * k + 1] = MERCATOR_RADIUS * Math.log(Math.tan(Math.PI / 4 + latRad / 2));
            xyz[3 * k + 2] = (height + 20) / cosLat;
            double u = (k % N - (N - 1) / 2.0) / ((N - 1) / 2.0), v = (k / N - (N - 1) / 2.0) / ((N - 1) / 2.0), heat = 0;
            for (double[] spot : spots) {
                double du = u - spot[0], dv = v - spot[1];
                heat += spot[3] * Math.exp(-(du * du + dv * dv) / (2 * spot[2] * spot[2]));
            }
            values[k] = (float) Math.min(1, heat);
        }
        nativeSetHeatmap(xyz, values, N);
    }

    private CustomHeatmap() {
    }
}
