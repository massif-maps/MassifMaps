#include "ui/WebMapView.h"
#include "renderers/MapRenderer.h"
#include "renderers/RedrawRequestListener.h"
#include "utils/Log.h"

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

namespace massif {

    // The event codes BaseMapView::onInputEvent takes.
    namespace {
        const int INPUT_EVENT_POINTER1_DOWN = 0;
        const int INPUT_EVENT_POINTER2_DOWN = 1;
        const int INPUT_EVENT_MOVE = 2;
        const int INPUT_EVENT_CANCEL = 3;
        const int INPUT_EVENT_POINTER1_UP = 4;
        const int INPUT_EVENT_POINTER2_UP = 5;

        const float NO_COORDINATE = -1.0f;
    }

    class WebMapView::RedrawListener : public RedrawRequestListener {
    public:
        explicit RedrawListener(WebMapView* view) : _view(view) { }

        virtual void onRedrawRequested() const {
            _view->requestRedraw();
        }

    private:
        WebMapView* const _view;
    };

    WebMapView::WebMapView(const std::string& canvasSelector) :
        _canvasSelector(canvasSelector)
    {
    }

    WebMapView::~WebMapView() {
        if (_surfaceCreated) {
            onSurfaceDestroyed();
        }
        if (_context) {
            emscripten_webgl_destroy_context(_context);
        }
    }

    bool WebMapView::start() {
        EmscriptenWebGLContextAttributes attributes;
        emscripten_webgl_init_context_attributes(&attributes);
        // WebGL 2 only: the renderer needs ES 3.0 entry points, and asking for 1 silently gives a
        // context where half of them are missing.
        attributes.majorVersion = 2;
        attributes.minorVersion = 0;
        attributes.alpha = false;
        attributes.depth = true;
        attributes.stencil = true;
        attributes.antialias = false;
        attributes.preserveDrawingBuffer = false;
        attributes.premultipliedAlpha = true;

        _context = emscripten_webgl_create_context(_canvasSelector.c_str(), &attributes);
        if (_context <= 0) {
            Log::Errorf("WebMapView::start: No WebGL 2 context on %s", _canvasSelector.c_str());
            return false;
        }
        if (emscripten_webgl_make_context_current(_context) != EMSCRIPTEN_RESULT_SUCCESS) {
            Log::Error("WebMapView::start: Failed to make the WebGL context current");
            return false;
        }

        onSurfaceCreated();
        _surfaceCreated = true;
        syncCanvasSize();

        setRedrawRequestListener(std::make_shared<RedrawListener>(this));

        emscripten_set_mousedown_callback(_canvasSelector.c_str(), this, EM_FALSE, OnPointer);
        emscripten_set_mousemove_callback(_canvasSelector.c_str(), this, EM_FALSE, OnPointer);
        emscripten_set_mouseup_callback(_canvasSelector.c_str(), this, EM_FALSE, OnPointer);
        emscripten_set_touchstart_callback(_canvasSelector.c_str(), this, EM_FALSE, OnTouch);
        emscripten_set_touchmove_callback(_canvasSelector.c_str(), this, EM_FALSE, OnTouch);
        emscripten_set_touchend_callback(_canvasSelector.c_str(), this, EM_FALSE, OnTouch);
        emscripten_set_touchcancel_callback(_canvasSelector.c_str(), this, EM_FALSE, OnTouch);
        emscripten_set_wheel_callback(_canvasSelector.c_str(), this, EM_FALSE, OnWheel);
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_FALSE, OnResize);

        requestRedraw();
        return true;
    }

    void WebMapView::requestRedraw() {
        if (_redrawPending.exchange(true)) {
            return;
        }
        // The tile and label workers ask for a redraw too, and requestAnimationFrame exists only on
        // the main thread - called from a worker it does nothing, and the map freezes after the
        // first frame.
        if (emscripten_is_main_browser_thread()) {
            emscripten_request_animation_frame(OnFrame, this);
        } else {
            emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, reinterpret_cast<void*>(&WebMapView::RequestFrameOnMainThread), this);
        }
    }

    void WebMapView::RequestFrameOnMainThread(void* userData) {
        emscripten_request_animation_frame(OnFrame, userData);
    }

    void WebMapView::syncCanvasSize() {
        double width = 0, height = 0;
        if (emscripten_get_element_css_size(_canvasSelector.c_str(), &width, &height) != EMSCRIPTEN_RESULT_SUCCESS) {
            return;
        }
        double pixelRatio = emscripten_get_device_pixel_ratio();
        int pixelWidth = static_cast<int>(width * pixelRatio);
        int pixelHeight = static_cast<int>(height * pixelRatio);
        if (pixelWidth <= 0 || pixelHeight <= 0 || (pixelWidth == _width && pixelHeight == _height)) {
            return;
        }
        _width = pixelWidth;
        _height = pixelHeight;
        emscripten_set_canvas_element_size(_canvasSelector.c_str(), _width, _height);
        onSurfaceChanged(_width, _height);
        requestRedraw();
    }

    EM_BOOL WebMapView::OnFrame(double time, void* userData) {
        WebMapView* view = static_cast<WebMapView*>(userData);
        view->_redrawPending = false;
        if (!view->_surfaceCreated) {
            return EM_FALSE;
        }
        emscripten_webgl_make_context_current(view->_context);
        view->onDrawFrame();
        return EM_FALSE;
    }

    EM_BOOL WebMapView::OnPointer(int eventType, const EmscriptenMouseEvent* event, void* userData) {
        WebMapView* view = static_cast<WebMapView*>(userData);
        double pixelRatio = emscripten_get_device_pixel_ratio();
        float x = static_cast<float>(event->targetX * pixelRatio);
        float y = static_cast<float>(event->targetY * pixelRatio);
        switch (eventType) {
        case EMSCRIPTEN_EVENT_MOUSEDOWN:
            view->_pointerDown = true;
            view->onInputEvent(INPUT_EVENT_POINTER1_DOWN, x, y, NO_COORDINATE, NO_COORDINATE);
            break;
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            if (!view->_pointerDown) {
                return EM_FALSE;
            }
            view->onInputEvent(INPUT_EVENT_MOVE, x, y, NO_COORDINATE, NO_COORDINATE);
            break;
        case EMSCRIPTEN_EVENT_MOUSEUP:
            view->_pointerDown = false;
            view->onInputEvent(INPUT_EVENT_POINTER1_UP, x, y, NO_COORDINATE, NO_COORDINATE);
            break;
        default:
            return EM_FALSE;
        }
        return EM_TRUE;
    }

    EM_BOOL WebMapView::OnTouch(int eventType, const EmscriptenTouchEvent* event, void* userData) {
        WebMapView* view = static_cast<WebMapView*>(userData);
        double pixelRatio = emscripten_get_device_pixel_ratio();

        // Only the first two touches reach the SDK - the gestures it knows use one or two pointers.
        float x1 = NO_COORDINATE, y1 = NO_COORDINATE, x2 = NO_COORDINATE, y2 = NO_COORDINATE;
        int touchCount = 0;
        for (int i = 0; i < event->numTouches && touchCount < 2; i++) {
            const EmscriptenTouchPoint& touch = event->touches[i];
            float x = static_cast<float>(touch.targetX * pixelRatio);
            float y = static_cast<float>(touch.targetY * pixelRatio);
            if (touchCount == 0) {
                x1 = x;
                y1 = y;
            } else {
                x2 = x;
                y2 = y;
            }
            touchCount++;
        }

        switch (eventType) {
        case EMSCRIPTEN_EVENT_TOUCHSTART:
            view->onInputEvent(touchCount >= 2 ? INPUT_EVENT_POINTER2_DOWN : INPUT_EVENT_POINTER1_DOWN, x1, y1, x2, y2);
            break;
        case EMSCRIPTEN_EVENT_TOUCHMOVE:
            view->onInputEvent(INPUT_EVENT_MOVE, x1, y1, x2, y2);
            break;
        case EMSCRIPTEN_EVENT_TOUCHEND:
            view->onInputEvent(touchCount >= 2 ? INPUT_EVENT_POINTER2_UP : INPUT_EVENT_POINTER1_UP, x1, y1, x2, y2);
            break;
        case EMSCRIPTEN_EVENT_TOUCHCANCEL:
            view->onInputEvent(INPUT_EVENT_CANCEL, x1, y1, x2, y2);
            break;
        default:
            return EM_FALSE;
        }
        return EM_TRUE;
    }

    EM_BOOL WebMapView::OnWheel(int eventType, const EmscriptenWheelEvent* event, void* userData) {
        WebMapView* view = static_cast<WebMapView*>(userData);
        double pixelRatio = emscripten_get_device_pixel_ratio();
        // deltaY grows downwards in the browser and upwards in the SDK, and a trackpad reports it
        // in fractional pixels - one tick per event is what the desktop hosts do too.
        int delta = event->deltaY > 0 ? -1 : 1;
        view->onWheelEvent(delta, static_cast<float>(event->mouse.targetX * pixelRatio), static_cast<float>(event->mouse.targetY * pixelRatio));
        return EM_TRUE;
    }

    EM_BOOL WebMapView::OnResize(int eventType, const EmscriptenUiEvent* event, void* userData) {
        static_cast<WebMapView*>(userData)->syncCanvasSize();
        return EM_FALSE;
    }

}
