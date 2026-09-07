/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_WEBMAPVIEW_H_
#define _MASSIF_WEBMAPVIEW_H_

#include "ui/BaseMapView.h"

#include <atomic>
#include <memory>
#include <string>

#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

namespace massif {

    /**
     * The host of a map on a canvas element: the WebGL 2 context, the animation-frame loop and the
     * pointer events, which is everything BaseMapView needs from a platform.
     *
     * Everything here runs on the browser's main thread - the SDK's own worker threads never touch
     * GL. A frame is drawn only when the renderer asks for one, same as the Android and iOS hosts.
     */
    class WebMapView : public BaseMapView {
    public:
        /**
         * @param canvasSelector The CSS selector of the canvas to render into ("#map").
         */
        explicit WebMapView(const std::string& canvasSelector);
        virtual ~WebMapView();

        /**
         * Creates the WebGL 2 context, registers the event handlers and starts the frame loop.
         * @return True if the context was created.
         */
        bool start();

        /**
         * Asks for one more frame. Called by the redraw request listener and after a resize, so it
         * has to be safe from the SDK's worker threads.
         */
        void requestRedraw();

    private:
        class RedrawListener;

        static EM_BOOL OnFrame(double time, void* userData);
        static void RequestFrameOnMainThread(void* userData);
        static EM_BOOL OnPointer(int eventType, const EmscriptenMouseEvent* event, void* userData);
        static EM_BOOL OnTouch(int eventType, const EmscriptenTouchEvent* event, void* userData);
        static EM_BOOL OnWheel(int eventType, const EmscriptenWheelEvent* event, void* userData);
        static EM_BOOL OnResize(int eventType, const EmscriptenUiEvent* event, void* userData);
        static EM_BOOL OnBlur(int eventType, const EmscriptenFocusEvent* event, void* userData);

        void syncCanvasSize();
        void applyDragRotate(float x, float y, double pixelRatio);
        void canvasPos(const EmscriptenMouseEvent* event, double pixelRatio, float& x, float& y) const;
        void cancelDrag();

        const std::string _canvasSelector;
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE _context = 0;
        std::atomic<bool> _redrawPending{false};
        bool _surfaceCreated = false;
        int _width = 0;
        int _height = 0;
        bool _pointerDown = false;
        // Right button, or left with ctrl: maplibre's trigger for rotate-and-pitch.
        bool _dragRotating = false;
        float _lastPointerX = 0.0f;
        float _lastPointerY = 0.0f;
    };

}

#endif
