#ifndef _MASSIF_API_MAPVIEWHOST_H_
#define _MASSIF_API_MAPVIEWHOST_H_

#include <memory>
#include <string>

namespace massif {
    class RedrawRequestListener;

    namespace api {

        /**
         * A map view the SDK creates, owns and registers itself, so a binding needs no object API
         * to host one: a platform view calls these with the view's handle and nothing else. The
         * Options and Layers are registered under the same id, so the rest of the map is reached
         * through the facade.
         */
        class MapViewHost {
        public:
            /**
             * Creates a view and registers it as `view:<id>`, its Options as `map:<id>` and its
             * Layers as `layers:<id>`. The map's events are emitted on the Options handle.
             *
             * @param id The map's id, shared by the three kinds.
             * @param dpi The screen density; 0 keeps the SDK default.
             * @return The view's handle, or 0 when one of the ids is taken.
             */
            static int create(const std::string& id, float dpi);

            /** Releases the three ids and stops the view calling its redraw listener. */
            static void destroy(const std::string& id);

            static void setRedrawRequestListener(int view, const std::shared_ptr<RedrawRequestListener>& listener);

            static void onSurfaceCreated(int view);
            static void onSurfaceChanged(int view, int width, int height);
            static void onSurfaceDestroyed(int view);
            static void onDrawFrame(int view);

            /** event is the platform-independent action code BaseMapView::onInputEvent takes. */
            static void onInputEvent(int view, int event, float x1, float y1, float x2, float y2);
        };

    }
}

#endif
