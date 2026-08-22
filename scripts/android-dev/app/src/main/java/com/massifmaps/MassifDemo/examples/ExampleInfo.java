package com.massifmaps.MassifDemo.examples;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * What an example is called and where it belongs, next to the code that IS the example.
 *
 * One source of truth on purpose. The gallery reads it at runtime, and scripts/gen-examples.py
 * reads the same annotation out of the .java file to write docs/examples/examples.json, which is
 * what the website builds its own gallery from. Adding an example file is therefore the whole
 * job - nothing else has a list to update.
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.TYPE)
public @interface ExampleInfo {

    /**
     * Stable, kebab-case, unique across every section. It names the screenshot
     * (docs/examples/screenshots/&lt;id&gt;.png) and the website's URL, so renaming one breaks both.
     */
    String id();

    /** One line, sentence case, as it appears in the grid. */
    String title();

    /** One or two sentences: what it shows and what to look at. Rendered under the title. */
    String description();

    /** A section id from {@link Sections}. An unknown one is reported by the generator. */
    String section();

    /** Position within the section. Ties fall back to the title. */
    int order() default 100;
}
