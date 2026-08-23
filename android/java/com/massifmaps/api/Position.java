package com.massifmaps.api;

/**
 * A position on the facade: longitude, latitude and an optional altitude, in WGS84.
 *
 * Hand-written and facade-owned rather than {@code com.massifmaps.core.MapPos}, which is a SWIG
 * proxy with a native peer - one C++ allocation across JNI per position, plus a finalizer, to
 * carry two doubles. A click handler pays that per event. This is a plain object.
 *
 * <b>Longitude first</b>, like GeoJSON and like the JSON the facade actually carries
 * (<code>[x, y]</code>). Not named LatLng for exactly that reason: a latitude-first type sitting
 * next to a longitude-first wire format is a swapped-coordinate bug waiting to happen.
 *
 * Kotlin destructures it: <pre>val (lng, lat) = map.getPos("clickPos")</pre>
 */
public final class Position {

    public final double lng;
    public final double lat;
    /** Metres above the ellipsoid. 0 when the value carried none. */
    public final double alt;

    public Position(double lng, double lat) {
        this(lng, lat, 0);
    }

    public Position(double lng, double lat, double alt) {
        this.lng = lng;
        this.lat = lat;
        this.alt = alt;
    }

    /** For Kotlin destructuring. */
    public double component1() {
        return lng;
    }

    public double component2() {
        return lat;
    }

    public double component3() {
        return alt;
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof Position)) {
            return false;
        }
        Position position = (Position) other;
        return lng == position.lng && lat == position.lat && alt == position.alt;
    }

    @Override
    public int hashCode() {
        return (int) (Double.doubleToLongBits(lng) ^ Double.doubleToLongBits(lat)
                      ^ Double.doubleToLongBits(alt));
    }

    @Override
    public String toString() {
        return "Position(" + lng + ", " + lat + (alt != 0 ? ", " + alt : "") + ")";
    }
}
