package com.massifmaps.api;

/**
 * A live event subscription.
 *
 * AutoCloseable so removal is the language's own idiom rather than a matching call an app has to
 * remember - try-with-resources for a scoped one, a field and close() in onDestroy for the rest.
 * Closing twice is harmless.
 */
public final class Subscription implements AutoCloseable {

    private int id;

    Subscription(int id) {
        this.id = id;
    }

    /** Whether the subscription is still live. */
    public boolean isActive() {
        return id != 0;
    }

    /** Removes it. Idempotent. */
    @Override
    public void close() {
        if (id != 0) {
            MassifApi.off(id);
            id = 0;
        }
    }
}
