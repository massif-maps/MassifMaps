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

    /** A line of text along the bottom telling the user what to do. Null or empty hides it. */
    void caption(String text);

    /** A push button in the control row. */
    void button(String label, Runnable action);

    /** An on/off button in the control row, starting in the given state. */
    void toggle(String label, boolean on, OnToggle action);

    /** A short message. Use sparingly - a caption is usually the better place. */
    void toast(String text);

    /** Runs something on the UI thread after a delay, cancelled when the example stops. */
    void postDelayed(Runnable action, long millis);

    interface OnToggle {
        void onToggle(boolean on);
    }
}
