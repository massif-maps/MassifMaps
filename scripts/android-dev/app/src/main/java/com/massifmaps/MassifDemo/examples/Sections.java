package com.massifmaps.MassifDemo.examples;

/**
 * The gallery's sections, in the order they are shown.
 *
 * Read by scripts/gen-examples.py as well as by the app, so the website's gallery is grouped and
 * ordered the same way. Adding a section is one entry here plus examples that name it.
 */
public final class Sections {

    public static final String BASICS = "basics";
    public static final String CAMERA = "camera";
    public static final String SOURCES = "sources";
    public static final String STYLES = "styles";
    public static final String TERRAIN = "terrain";
    public static final String ANNOTATIONS = "annotations";
    public static final String INTERACTION = "interaction";
    public static final String SEARCH = "search";

    /** Section id, title and one-line blurb, in display order. */
    public static final String[][] ALL = {
        { BASICS, "Map basics", "Put a map on screen and point it somewhere." },
        { CAMERA, "Camera", "Move, fly, frame and constrain the view." },
        { SOURCES, "Sources & data", "Where tiles and features come from." },
        { STYLES, "Styles & layers", "CartoCSS, style projects and layer composition." },
        { TERRAIN, "3D terrain", "Elevation, hillshade, sky and fog." },
        { ANNOTATIONS, "Markers & popups", "Things an app puts on the map itself." },
        { INTERACTION, "Interaction", "Clicks, features and live updates." },
        { SEARCH, "Search & routing", "Finding features and getting from A to B." },
    };

    /** The display title of a section id, or the id itself when it is not one of ours. */
    public static String titleOf(String id) {
        for (String[] section : ALL) {
            if (section[0].equals(id)) {
                return section[1];
            }
        }
        return id;
    }

    private Sections() {
    }
}
