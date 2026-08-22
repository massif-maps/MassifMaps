package com.massifmaps.MassifDemo.examples;

/**
 * One example.
 *
 * The whole contract is {@link #onStart}: the map is attached and on screen by the time it runs,
 * and everything the example builds is torn down for it. Subclasses are read as documentation, so
 * they should contain map code and as little Android as possible - {@link ExampleHost} owns the
 * buttons, captions and toasts for that reason.
 *
 * An abstract class rather than an interface so an example that needs no teardown writes no
 * teardown.
 */
public abstract class MapExample {

    /**
     * Called once the map view has a size and the map is attached.
     *
     * On a WORKER thread, not the UI one: building a layer decodes its style, which takes seconds
     * for a real style project and would be an "isn't responding" dialog before the map appeared.
     * Every {@link ExampleHost} method is safe to call from here.
     */
    public abstract void onStart(ExampleHost host);

    /**
     * Called when the screen goes away. Only for things the host cannot release itself - a timer,
     * a sensor, a thread. Layers, sources and subscriptions the example created are closed with
     * the map.
     */
    public void onStop() {
    }
}
