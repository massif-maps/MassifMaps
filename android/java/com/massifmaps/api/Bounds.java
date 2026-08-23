package com.massifmaps.api;

/**
 * A bounding box on the facade, as a pair of {@link Position}s.
 *
 * The facade's own type rather than {@code com.massifmaps.core.MapBounds}, for the same reason
 * {@link Position} is - see there.
 */
public final class Bounds {

    public final Position min;
    public final Position max;

    public Bounds(Position min, Position max) {
        this.min = min;
        this.max = max;
    }

    /** The corners as longitudes and latitudes, which is how a box is usually written down. */
    public Bounds(double minLng, double minLat, double maxLng, double maxLat) {
        this(new Position(minLng, minLat), new Position(maxLng, maxLat));
    }

    /** For Kotlin destructuring. */
    public Position component1() {
        return min;
    }

    public Position component2() {
        return max;
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof Bounds)) {
            return false;
        }
        Bounds bounds = (Bounds) other;
        return min.equals(bounds.min) && max.equals(bounds.max);
    }

    @Override
    public int hashCode() {
        return min.hashCode() * 31 + max.hashCode();
    }

    @Override
    public String toString() {
        return "Bounds(" + min + ", " + max + ")";
    }
}
