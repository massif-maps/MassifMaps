package com.massifmaps.api;

import com.massifmaps.core.MapPos;

/**
 * A view of one object scoped to a path prefix.
 *
 * <pre>map.fog().set("rangeStart", 2.5).set("rangeEnd", 8).set("enabled", true);</pre>
 *
 * Deliberately not a method per option. There are over seven hundred properties and they change
 * with the SDK; naming each one here would reintroduce exactly the per-platform maintenance the
 * facade exists to remove, and a new option would not work until someone added a method for it.
 * The prefix is what makes the string short enough to live with.
 */
public final class PropertyGroup {

    private final MassifObject target;
    private final String prefix;

    PropertyGroup(MassifObject target, String prefix) {
        this.target = target;
        this.prefix = prefix.isEmpty() || prefix.endsWith(".") ? prefix : prefix + ".";
    }

    public PropertyGroup set(String name, Object value) {
        target.set(prefix + name, value);
        return this;
    }

    public double getDouble(String name, double defaultValue) {
        return target.getDouble(prefix + name, defaultValue);
    }

    public long getLong(String name, long defaultValue) {
        return target.getLong(prefix + name, defaultValue);
    }

    public boolean getBool(String name, boolean defaultValue) {
        return target.getBool(prefix + name, defaultValue);
    }

    /** @param defaultValue May be null. */
    public String getString(String name, String defaultValue) {
        return target.getString(prefix + name, defaultValue);
    }

    public MapPos getPos(String name) {
        return target.getPos(prefix + name);
    }

    /** The object this group belongs to, for anything outside the prefix. */
    public MassifObject object() {
        return target;
    }

    @Override
    public String toString() {
        return "PropertyGroup(" + prefix + ")";
    }
}
