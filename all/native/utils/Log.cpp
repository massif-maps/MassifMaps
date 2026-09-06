#include "Log.h"
#include "utils/LogEventListener.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#include <os/log.h>
#include <cstdio>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace massif {

#ifdef __ANDROID__
    enum LogType { LOG_TYPE_FATAL = ANDROID_LOG_ERROR, LOG_TYPE_ERROR = ANDROID_LOG_ERROR, LOG_TYPE_WARNING = ANDROID_LOG_WARN, LOG_TYPE_INFO = ANDROID_LOG_INFO, LOG_TYPE_DEBUG = ANDROID_LOG_DEBUG };

    static void OutputLog(LogType logType, const std::string& tag, const char* text) {
        __android_log_print(static_cast<int>(logType), tag.c_str(), "%s", text);
    }
#endif
#ifdef __APPLE__
    enum LogType { LOG_TYPE_FATAL, LOG_TYPE_ERROR, LOG_TYPE_WARNING, LOG_TYPE_INFO, LOG_TYPE_DEBUG };

    // asl_log, which this used, has been inert since iOS 10: it never reached the unified log, so
    // every Log:: call on iOS went nowhere and 'log stream' showed nothing. os_log at the default
    // level, so the message survives to 'log stream' without needing --level info; %{public}s
    // because os_log redacts a plain %s as <private>.
    static void OutputLog(LogType logType, const std::string& tag, const char* text) {
#if TARGET_OS_IPHONE
        static os_log_t logHandle = os_log_create("com.massifmaps.sdk", "sdk");
        if (logType == LOG_TYPE_FATAL || logType == LOG_TYPE_ERROR) {
            os_log_error(logHandle, "%{public}s", text);
        } else {
            os_log(logHandle, "%{public}s", text);
        }
#else
        // Host builds (the ctest suite): stderr is what the test runner shows.
        std::fprintf(stderr, "%s: %s\n", tag.c_str(), text);
#endif
    }
#endif
#ifdef _WIN32
    enum LogType { LOG_TYPE_FATAL, LOG_TYPE_ERROR, LOG_TYPE_WARNING, LOG_TYPE_INFO, LOG_TYPE_DEBUG };
    
    static void OutputLog(LogType logType, const std::string& tag, const char* text) {
        OutputDebugStringA(text);
        OutputDebugStringA("\n");
    }
#endif

    bool Log::IsShowError() {
        std::lock_guard<std::mutex> lock(_Mutex);
        return _ShowError;
    }

    void Log::SetShowError(bool showError) {
        std::lock_guard<std::mutex> lock(_Mutex);
        _ShowError = showError;
    }

    bool Log::IsShowWarn() {
        std::lock_guard<std::mutex> lock(_Mutex);
        return _ShowWarn;
    }

    void Log::SetShowWarn(bool showWarn) {
        std::lock_guard<std::mutex> lock(_Mutex);
        _ShowWarn = showWarn;
    }

    bool Log::IsShowInfo() {
        std::lock_guard<std::mutex> lock(_Mutex);
        return _ShowInfo;
    }

    void Log::SetShowInfo(bool showInfo) {
        std::lock_guard<std::mutex> lock(_Mutex);
        _ShowInfo = showInfo;
    }

    bool Log::IsShowDebug() {
        std::lock_guard<std::mutex> lock(_Mutex);
        return _ShowDebug;
    }

    void Log::SetShowDebug(bool showDebug) {
        std::lock_guard<std::mutex> lock(_Mutex);
        _ShowDebug = showDebug;
    }

    std::string Log::GetTag() {
        std::lock_guard<std::mutex> lock(_Mutex);
        return _Tag;
    }

    void Log::SetTag(const std::string& tag) {
        std::lock_guard<std::mutex> lock(_Mutex);
        _Tag = tag;
    }

    std::shared_ptr<LogEventListener> Log::GetLogEventListener() {
        return _LogEventListener.get();
    }
    
    void Log::SetLogEventListener(const std::shared_ptr<LogEventListener>& listener) {
        _LogEventListener.set(listener);
    }

    void Log::Fatal(const char* message) {
        DirectorPtr<LogEventListener> logEventListener = _LogEventListener;
        if (logEventListener) {
            if (!logEventListener->onFatalEvent(message)) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        OutputLog(LOG_TYPE_FATAL, _Tag, message);
    }

    void Log::Error(const char* message) {
        DirectorPtr<LogEventListener> logEventListener = _LogEventListener;
        if (logEventListener) {
            if (!logEventListener->onErrorEvent(message)) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        if (_ShowError) {
            OutputLog(LOG_TYPE_ERROR, _Tag, message);
        }
    }

    void Log::Warn(const char* message) {
        DirectorPtr<LogEventListener> logEventListener = _LogEventListener;
        if (logEventListener) {
            if (!logEventListener->onWarnEvent(message)) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        if (_ShowWarn) {
            OutputLog(LOG_TYPE_WARNING, _Tag, message);
        }
    }

    void Log::Info(const char* message) {
        DirectorPtr<LogEventListener> logEventListener = _LogEventListener;
        if (logEventListener) {
            if (!logEventListener->onInfoEvent(message)) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        if (_ShowInfo) {
            OutputLog(LOG_TYPE_INFO, _Tag, message);
        }
    }

    void Log::Debug(const char* message) {
        DirectorPtr<LogEventListener> logEventListener = _LogEventListener;
        if (logEventListener) {
            if (!logEventListener->onDebugEvent(message)) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        if (_ShowDebug) {
            OutputLog(LOG_TYPE_DEBUG, _Tag, message);
        }
    }

    Log::Log() {
    }

    bool Log::_ShowError = true;
    bool Log::_ShowWarn = true;
    bool Log::_ShowInfo = true;
    bool Log::_ShowDebug = false;

    std::string Log::_Tag = "massif";

    DirectorPtr<LogEventListener> Log::_LogEventListener;

    std::mutex Log::_Mutex;

}
