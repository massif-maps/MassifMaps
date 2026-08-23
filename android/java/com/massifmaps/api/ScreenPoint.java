package com.massifmaps.api;

/**
 * A point in view coordinates - pixels from the top-left of the map view.
 *
 * The facade's own type rather than {@code com.massifmaps.core.ScreenPos}, so the whole sugar
 * layer names no SWIG proxy. Not a {@link Position}: it carries no coordinate system and is never
 * reprojected.
 */
public final class ScreenPoint {

    public final float x;
    public final float y;

    public ScreenPoint(float x, float y) {
        this.x = x;
        this.y = y;
    }

    /** For Kotlin destructuring. */
    public float component1() {
        return x;
    }

    public float component2() {
        return y;
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof ScreenPoint
               && x == ((ScreenPoint) other).x && y == ((ScreenPoint) other).y;
    }

    @Override
    public int hashCode() {
        return Float.floatToIntBits(x) ^ Float.floatToIntBits(y);
    }

    @Override
    public String toString() {
        return "ScreenPoint(" + x + ", " + y + ")";
    }
}
