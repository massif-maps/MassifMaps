#include "ui/WebMapView.h"
#include "components/Options.h"
#include "core/MapPos.h"
#include "core/ScreenPos.h"
#include "renderers/MapRenderer.h"
#include "renderers/RedrawRequestListener.h"
#include "utils/Const.h"
#include "utils/Log.h"

#include <cmath>

#include <emscripten/emscripten.h>
#include <emscripten/em_asm.h>
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

        // Ported from maplibre-gl-js so a mouse feels the same here as on any other web map:
        // src/ui/handler/mouse.ts (rotateSpeed, pitchSpeed) and scroll_zoom.ts (the zoom rates and
        // the sigmoid). Degrees per CSS pixel of drag, and zoom per unit of wheel delta.
        //
        // Both are NEGATED against maplibre's, because both angles are measured the other way here:
        // maplibre turns the camera's bearing where rotate() turns the map under it, and this SDK's
        // tilt is 90 at straight down where mapbox's pitch is 0 there. Taken as written, a
        // right-drag moved the map the wrong way on both axes.
        const float ROTATE_SPEED = -0.8f;
        const float PITCH_SPEED = 0.5f;
        const double WHEEL_ZOOM_RATE = 1.0 / 450.0;
        const double TRACKPAD_ZOOM_RATE = 1.0 / 100.0;
        const double MAX_SCALE_PER_WHEEL_EVENT = 2.0;
        // A line-mode wheel event carries lines, not pixels; 40 is what the browsers agreed on.
        const double WHEEL_LINE_HEIGHT = 40.0;

        const float DOUBLE_CLICK_ZOOM_DURATION = 0.3f;

        // maplibre's clickTolerance, in dp - which on the web is a CSS pixel.
        const float MOUSE_CLICK_MOVING_TOLERANCE = 3.0f;

        // The SDK's default is 16, chosen for a phone. Tiles are cheap to keep on a desktop GPU and
        // a tilted web map is expected to draw into the distance.
        const float WEB_DRAW_DISTANCE = 96.0f;

        // maplibre and mapbox-gl calibrate zoom on a 512-pixel tile where the SDK uses 256, so the
        // same zoom NUMBER is one level closer there. On the web that difference is visible: a
        // link, a style's zoom stops and every piece of advice about web maps assume their
        // convention. Every other platform keeps the SDK's own - see Options::setZoomOffset.
        const float WEB_ZOOM_OFFSET = 1.0f;
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
        emscripten_set_dblclick_callback(_canvasSelector.c_str(), this, EM_FALSE, OnDoubleClick);
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_FALSE, OnResize);

        // A right-drag rotates the map, so the canvas must not open the browser's context menu.
        EM_ASM({
            var canvas = document.querySelector(UTF8ToString($0));
            if (canvas) {
                canvas.addEventListener('contextmenu', function(event) { event.preventDefault(); });
            }
        }, _canvasSelector.c_str());

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
        // Same as the iOS host does with UIScreen's scale. Without it the SDK takes the canvas for a
        // 1x screen, draws every tile at half the size it should be, and asks for four times as
        // many. Set here rather than in start() so it follows a window moved to another display.
        getOptions()->setDPI(Const::UNSCALED_DPI * static_cast<float>(pixelRatio));
        // A mouse is not a finger: the SDK's 32 dp default made a drag feel stuck for its first
        // half-centimetre. 3 is maplibre's clickTolerance.
        getOptions()->setClickMovingTolerance(MOUSE_CLICK_MOVING_TOLERANCE);
        // A desktop GPU is not a phone: 16 keeps buildings and terrain to a near band when the map
        // is tilted, which is a mobile battery decision. Mapbox and maplibre draw to the horizon.
        getOptions()->setDrawDistance(WEB_DRAW_DISTANCE);
        // Zoom 14 here means what zoom 14 means in maplibre.
        getOptions()->setZoomOffset(WEB_ZOOM_OFFSET);
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

    void WebMapView::applyDragRotate(float x, float y, double pixelRatio) {
        // Degrees per CSS pixel, which is what maplibre's speeds are in - dividing the device-pixel
        // delta back out keeps a drag turning the map by the same amount on a 2x display.
        rotate(static_cast<float>((x - _lastPointerX) / pixelRatio) * ROTATE_SPEED, 0);
        tilt(static_cast<float>((y - _lastPointerY) / pixelRatio) * PITCH_SPEED, 0);
        _lastPointerX = x;
        _lastPointerY = y;
    }

    EM_BOOL WebMapView::OnPointer(int eventType, const EmscriptenMouseEvent* event, void* userData) {
        WebMapView* view = static_cast<WebMapView*>(userData);
        double pixelRatio = emscripten_get_device_pixel_ratio();
        float x = static_cast<float>(event->targetX * pixelRatio);
        float y = static_cast<float>(event->targetY * pixelRatio);
        const int RIGHT_BUTTON = 2;

        switch (eventType) {
        case EMSCRIPTEN_EVENT_MOUSEDOWN:
            view->_lastPointerX = x;
            view->_lastPointerY = y;
            if (event->button == RIGHT_BUTTON || event->ctrlKey) {
                view->_dragRotating = true;
            } else {
                view->_pointerDown = true;
                view->onInputEvent(INPUT_EVENT_POINTER1_DOWN, x, y, NO_COORDINATE, NO_COORDINATE);
            }
            break;
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            if (view->_dragRotating) {
                view->applyDragRotate(x, y, pixelRatio);
            } else if (view->_pointerDown) {
                view->onInputEvent(INPUT_EVENT_MOVE, x, y, NO_COORDINATE, NO_COORDINATE);
            } else {
                return EM_FALSE;
            }
            break;
        case EMSCRIPTEN_EVENT_MOUSEUP:
            if (view->_dragRotating) {
                view->_dragRotating = false;
            } else if (view->_pointerDown) {
                view->_pointerDown = false;
                view->onInputEvent(INPUT_EVENT_POINTER1_UP, x, y, NO_COORDINATE, NO_COORDINATE);
            }
            break;
        default:
            return EM_FALSE;
        }
        return EM_TRUE;
    }

    EM_BOOL WebMapView::OnDoubleClick(int eventType, const EmscriptenMouseEvent* event, void* userData) {
        WebMapView* view = static_cast<WebMapView*>(userData);
        double pixelRatio = emscripten_get_device_pixel_ratio();
        ScreenPos screenPos(static_cast<float>(event->targetX * pixelRatio), static_cast<float>(event->targetY * pixelRatio));
        // Shift halves instead of doubling, same as every other web map.
        view->zoom(event->shiftKey ? -1.0f : 1.0f, view->screenToMap(screenPos), DOUBLE_CLICK_ZOOM_DURATION);
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

        double delta = event->deltaY;
        if (event->deltaMode == DOM_DELTA_LINE) {
            delta *= WHEEL_LINE_HEIGHT;
        } else if (event->deltaMode == DOM_DELTA_PAGE) {
            delta *= WHEEL_LINE_HEIGHT * 20;
        }
        if (delta == 0) {
            return EM_FALSE;
        }

        // A trackpad pinch reaches the page as a wheel event with ctrl held, and it needs the
        // coarser rate - a mouse wheel's single notch is a much bigger delta than a pinch frame.
        double rate = event->mouse.ctrlKey ? TRACKPAD_ZOOM_RATE : WHEEL_ZOOM_RATE;
        double scale = MAX_SCALE_PER_WHEEL_EVENT / (1.0 + std::exp(-std::abs(delta * rate)));
        if (delta > 0) {
            scale = 1.0 / scale;
        }

        ScreenPos screenPos(static_cast<float>(event->mouse.targetX * pixelRatio), static_cast<float>(event->mouse.targetY * pixelRatio));
        view->zoom(static_cast<float>(std::log2(scale)), view->screenToMap(screenPos), 0);
        return EM_TRUE;
    }

    EM_BOOL WebMapView::OnResize(int eventType, const EmscriptenUiEvent* event, void* userData) {
        static_cast<WebMapView*>(userData)->syncCanvasSize();
        return EM_FALSE;
    }

}
