package com.massifmaps.api;

/**
 * A rectangle in view coordinates - the part of the view a {@code fitBounds} should frame, when
 * something overlays the rest.
 *
 * The facade's own type rather than {@code com.massifmaps.core.ScreenBounds}; see
 * {@link ScreenPoint}.
 */
public final class ScreenRect {

    public final ScreenPoint min;
    public final ScreenPoint max;

    public ScreenRect(ScreenPoint min, ScreenPoint max) {
        this.min = min;
        this.max = max;
    }

    /** The usual form: a rectangle from the top-left corner. */
    public ScreenRect(float left, float top, float right, float bottom) {
        this(new ScreenPoint(left, top), new ScreenPoint(right, bottom));
    }

    /** For Kotlin destructuring. */
    public ScreenPoint component1() {
        return min;
    }

    public ScreenPoint component2() {
        return max;
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof ScreenRect)) {
            return false;
        }
        ScreenRect rect = (ScreenRect) other;
        return min.equals(rect.min) && max.equals(rect.max);
    }

    @Override
    public int hashCode() {
        return min.hashCode() * 31 + max.hashCode();
    }

    @Override
    public String toString() {
        return "ScreenRect(" + min + ", " + max + ")";
    }
}
