#include "api/MapViewHost.h"
#include "renderers/RedrawRequestListener.h"
#include "utils/AndroidUtils.h"
#include "utils/AssetUtils.h"
#include "utils/Log.h"

#include <jni.h>

#include <functional>
#include <memory>
#include <string>

// The Android face of api/MapViewHost.h, written by hand: MapView reaches the SDK through these
// functions and the facade, with no object-API proxy.

namespace {

    using massif::api::MapViewHost;

    /** Asks a Runnable on the Java side to requestRender(), from whichever thread the SDK asks on. */
    class RunnableRedrawListener : public massif::RedrawRequestListener {
    public:
        RunnableRedrawListener(JNIEnv* jenv, jobject runnable) {
            _runnable = jenv->NewGlobalRef(runnable);
            jclass runnableClass = jenv->FindClass("java/lang/Runnable");
            _run = jenv->GetMethodID(runnableClass, "run", "()V");
            jenv->DeleteLocalRef(runnableClass);
        }

        virtual ~RunnableRedrawListener() {
            // A global ref is released from any thread, but through that thread's own env.
            if (JNIEnv* jenv = massif::AndroidUtils::GetCurrentThreadJNIEnv()) {
                jenv->DeleteGlobalRef(_runnable);
            }
        }

        virtual void onRedrawRequested() const {
            JNIEnv* jenv = massif::AndroidUtils::GetCurrentThreadJNIEnv();
            if (!jenv) {
                return;
            }
            jenv->CallVoidMethod(_runnable, _run);
            if (jenv->ExceptionCheck()) {
                jenv->ExceptionClear();
            }
        }

    private:
        jobject _runnable = nullptr;
        jmethodID _run = nullptr;
    };

    /** A throw out of a native method kills the process: nothing here may leave one. */
    void guarded(const char* what, const std::function<void()>& call) {
        try {
            call();
        } catch (const std::exception& ex) {
            massif::Log::Errorf("MapViewNative::%s: %s", what, ex.what());
        } catch (...) {
            massif::Log::Errorf("MapViewNative::%s: unknown exception", what);
        }
    }

}

extern "C" {

JNIEXPORT void JNICALL Java_com_massifmaps_ui_MapViewNative_init(JNIEnv* jenv, jclass, jobject context, jobject assetManager) {
    massif::AndroidUtils::AttachJVM(jenv);
    massif::AndroidUtils::SetContext(context);
    if (assetManager) {
        massif::AssetUtils::SetAssetManagerPointer(assetManager);
    }
}

JNIEXPORT jint JNICALL Java_com_massifmaps_ui_MapViewNative_create(JNIEnv* jenv, jclass, jstring id, jfloat dpi, jobject redraw) {
    const char* chars = jenv->GetStringUTFChars(id, nullptr);
    std::string name(chars ? chars : "");
    if (chars) {
        jenv->ReleaseStringUTFChars(id, chars);
    }
    int handle = 0;
    guarded("create", [&] {
        handle = MapViewHost::create(name, dpi);
        if (handle && redraw) {
            MapViewHost::setRedrawRequestListener(handle, std::make_shared<RunnableRedrawListener>(jenv, redraw));
        }
    });
    return handle;
}

JNIEXPORT void JNICALL Java_com_massifmaps_ui_MapViewNative_destroy(JNIEnv* jenv, jclass, jstring id) {
    const char* chars = jenv->GetStringUTFChars(id, nullptr);
    std::string name(chars ? chars : "");
    if (chars) {
        jenv->ReleaseStringUTFChars(id, chars);
    }
    guarded("destroy", [&] { MapViewHost::destroy(name); });
}

JNIEXPORT void JNICALL Java_com_massifmaps_ui_MapViewNative_surfaceCreated(JNIEnv*, jclass, jint view) {
    guarded("surfaceCreated", [&] { MapViewHost::onSurfaceCreated(view); });
}

JNIEXPORT void JNICALL Java_com_massifmaps_ui_MapViewNative_surfaceChanged(JNIEnv*, jclass, jint view, jint width, jint height) {
    guarded("surfaceChanged", [&] { MapViewHost::onSurfaceChanged(view, width, height); });
}

JNIEXPORT void JNICALL Java_com_massifmaps_ui_MapViewNative_surfaceDestroyed(JNIEnv*, jclass, jint view) {
    guarded("surfaceDestroyed", [&] { MapViewHost::onSurfaceDestroyed(view); });
}

JNIEXPORT void JNICALL Java_com_massifmaps_ui_MapViewNative_drawFrame(JNIEnv*, jclass, jint view) {
    guarded("drawFrame", [&] { MapViewHost::onDrawFrame(view); });
}

JNIEXPORT void JNICALL Java_com_massifmaps_ui_MapViewNative_input(JNIEnv*, jclass, jint view, jint event, jfloat x1, jfloat y1, jfloat x2, jfloat y2) {
    guarded("input", [&] { MapViewHost::onInputEvent(view, event, x1, y1, x2, y2); });
}

}
