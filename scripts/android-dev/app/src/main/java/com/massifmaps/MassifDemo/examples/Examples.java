package com.massifmaps.MassifDemo.examples;

import java.util.ArrayList;
import java.util.List;

/**
 * The examples, as the app sees them.
 *
 * A thin read over {@link ExampleRegistry} (generated) plus each class's own {@link ExampleInfo}.
 * The annotation is read rather than copied into the generated file so the strings have exactly
 * one home - the example itself.
 */
public final class Examples {

    /** Where the screenshots and the sources are, inside the APK assets. See app/build.gradle. */
    public static final String SHOT_DIR = "screenshots/";
    public static final String SOURCE_DIR = "example-src/";

    /** One example, resolved. */
    public static final class Entry {
        public final Class<?> type;
        public final ExampleInfo info;

        Entry(Class<?> type, ExampleInfo info) {
            this.type = type;
            this.info = info;
        }

        public String id() {
            return info.id();
        }

        public String title() {
            return info.title();
        }

        public String description() {
            return info.description();
        }

        public String section() {
            return info.section();
        }

        /** The screenshot asset, whether or not it has been captured. */
        public String shotAsset() {
            return SHOT_DIR + info.id() + ".png";
        }

        /** The example's own .java, as an asset - the class name is the path. */
        public String sourceAsset() {
            String name = type.getName();
            int packageEnd = Examples.class.getPackage().getName().length() + 1;
            return SOURCE_DIR + name.substring(packageEnd).replace('.', '/') + ".java";
        }

        /** A new instance. The examples all have a no-argument constructor. */
        public MapExample create() throws Exception {
            return (MapExample) type.getDeclaredConstructor().newInstance();
        }
    }

    private static List<Entry> entries;

    /** Every example, in the order the generator put them in: by section, then by order. */
    public static synchronized List<Entry> all() {
        if (entries == null) {
            entries = new ArrayList<>();
            for (Class<?> type : ExampleRegistry.ALL) {
                ExampleInfo info = type.getAnnotation(ExampleInfo.class);
                if (info != null) {
                    entries.add(new Entry(type, info));
                }
            }
        }
        return entries;
    }

    /** @return null when no example has that id. */
    public static Entry byId(String id) {
        for (Entry entry : all()) {
            if (entry.id().equals(id)) {
                return entry;
            }
        }
        return null;
    }

    private Examples() {
    }
}
