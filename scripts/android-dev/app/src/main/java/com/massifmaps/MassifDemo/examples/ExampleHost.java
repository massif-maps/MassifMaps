package com.massifmaps.MassifDemo.examples;

import android.content.Context;

import com.massifmaps.api.MassifMap;

/**
 * The screen an example runs on.
 *
 * Everything Android-shaped lives behind this so an example file reads as map code: a control is
 * one call, not an inflate plus a listener plus a layout param. The controls appear in a row at
 * the bottom of the screen, above the caption.
 */
public interface ExampleHost {

    /**
     * The map, already attached and ready. Registered under the example's own id, so two examples
     * cannot collide, and closed - with every layer it built - when the screen goes away.
     */
    MassifMap map();

    /** For anything that genuinely needs a Context: an asset, a density, a dialog. */
    Context context();

    /**
     * An absolute path under the app's cache directory, for a tile cache database.
     *
     * Every example that reads from a remote server puts a PersistentCacheTileDataSource in front
     * of it: a demo the user pans around otherwise re-fetches the same tiles from somebody else's
     * free service on every run.
     */
    String cachePath(String name);

    /** A line of text along the bottom telling the user what to do. Null or empty hides it. */
    void caption(String text);

    /** A push button in the control row. */
    void button(String label, Runnable action);

    /** An on/off button in the control row, starting in the given state. */
    void toggle(String label, boolean on, OnToggle action);

    /**
     * A slider in the control row, for a value worth sweeping rather than picking - a duration, an
     * exaggeration. The label is shown with the current value appended.
     */
    void slider(String label, float min, float max, float value, OnValue action);

    /** A short message. Use sparingly - a caption is usually the better place. */
    void toast(String text);

    /** Runs something on the UI thread after a delay, cancelled when the example stops. */
    void postDelayed(Runnable action, long millis);

    interface OnToggle {
        void onToggle(boolean on);
    }

    interface OnValue {
        void onValue(float value);
    }
}
