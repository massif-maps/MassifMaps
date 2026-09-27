#include "TerrainDepthWorker.h"
#include "utils/Log.h"

// __ANDROID__ (always set by the NDK) rather than TARGET_OS_ANDROID: the latter is only passed
// for standalone syntax checks, so keying off it silently compiled the worker out of the build.
#if defined(__ANDROID__)
#define _MASSIF_TERRAINDEPTHWORKER_EGL 1
#endif

#if _MASSIF_TERRAINDEPTHWORKER_EGL
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <sys/system_properties.h>
#include <cstdlib>
#endif

namespace massif {

#if _MASSIF_TERRAINDEPTHWORKER_EGL

    namespace {
        GLuint CompileShader(GLenum type, const std::string& source) {
            GLuint shaderId = glCreateShader(type);
            const char* sourcePtr = source.c_str();
            GLint sourceLen = static_cast<GLint>(source.size());
            glShaderSource(shaderId, 1, &sourcePtr, &sourceLen);
            glCompileShader(shaderId);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shaderId, GL_COMPILE_STATUS, &compiled);
            if (!compiled) {
                glDeleteShader(shaderId);
                return 0;
            }
            return shaderId;
        }
    }

    bool TerrainDepthWorker::isSupported() {
        // The synchronous read-back stays reachable at runtime, so the two can be compared on
        // one device: 'adb shell setprop debug.massif.asyncdepth 0'.
        static const bool enabled = [] {
#if MASSIF_DEBUG_PROPERTIES
            char property[PROP_VALUE_MAX] = { 0 };
            return !(__system_property_get("debug.massif.asyncdepth", property) > 0 && property[0] == '0');
#else
            return true;
#endif
        }();
        return enabled;
    }

    int TerrainDepthWorker::getMovingSubmitInterval(int defaultInterval) {
        static const int interval = [defaultInterval] {
#if MASSIF_DEBUG_PROPERTIES
            char property[PROP_VALUE_MAX] = { 0 };
            if (__system_property_get("debug.massif.asyncdepthms", property) > 0) {
                int value = std::atoi(property);
                if (value >= 0) {
                    return value;
                }
            }
#endif
            return defaultInterval;
        }();
        return interval;
    }

    bool TerrainDepthWorker::initContext() {
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY) {
            return false;
        }
        // The render thread has already initialized the display; initializing it again only
        // bumps its reference count.
        if (!eglInitialize(display, NULL, NULL)) {
            return false;
        }
        _display = display;

        const EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
        };
        EGLConfig config = NULL;
        EGLint configCount = 0;
        if (!eglChooseConfig(display, configAttribs, &config, 1, &configCount) || configCount < 1) {
            return false;
        }

        // A 1x1 pbuffer: the depth pass renders into its own framebuffer, the surface only
        // exists because a context has to be made current on something.
        const EGLint surfaceAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
        if (surface == EGL_NO_SURFACE) {
            return false;
        }
        _surface = surface;

        const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
        if (context == EGL_NO_CONTEXT) {
            return false;
        }
        _context = context;
        if (!eglMakeCurrent(display, surface, surface, context)) {
            return false;
        }
        return initProgram();
    }

    bool TerrainDepthWorker::initProgram() {
        GLuint vertexShaderId = CompileShader(GL_VERTEX_SHADER, _vertexShaderSource);
        GLuint fragmentShaderId = CompileShader(GL_FRAGMENT_SHADER, _fragmentShaderSource);
        if (!vertexShaderId || !fragmentShaderId) {
            return false;
        }
        GLuint programId = glCreateProgram();
        glAttachShader(programId, vertexShaderId);
        glAttachShader(programId, fragmentShaderId);
        glLinkProgram(programId);
        glDeleteShader(vertexShaderId);
        glDeleteShader(fragmentShaderId);
        GLint linked = GL_FALSE;
        glGetProgramiv(programId, GL_LINK_STATUS, &linked);
        if (!linked) {
            glDeleteProgram(programId);
            return false;
        }
        _programId = programId;
        _aCoord = glGetAttribLocation(programId, "a_coord");
        _uMVPMat = glGetUniformLocation(programId, "u_mvpMat");
        _uFar = glGetUniformLocation(programId, "u_far");
        return _aCoord >= 0 && _uMVPMat >= 0;
    }

    bool TerrainDepthWorker::initFrameBuffer(int width, int height) {
        if (_frameBufferId && _frameBufferWidth == width && _frameBufferHeight == height) {
            return true;
        }
        if (_frameBufferId) {
            glDeleteFramebuffers(1, &_frameBufferId);
            glDeleteTextures(1, &_colorTextureId);
            glDeleteRenderbuffers(1, &_depthBufferId);
            _frameBufferId = _colorTextureId = _depthBufferId = 0;
        }

        glGenTextures(1, &_colorTextureId);
        glBindTexture(GL_TEXTURE_2D, _colorTextureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenRenderbuffers(1, &_depthBufferId);
        glBindRenderbuffer(GL_RENDERBUFFER, _depthBufferId);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);

        glGenFramebuffers(1, &_frameBufferId);
        glBindFramebuffer(GL_FRAMEBUFFER, _frameBufferId);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _colorTextureId, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthBufferId);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            return false;
        }
        _frameBufferWidth = width;
        _frameBufferHeight = height;
        return true;
    }

    void TerrainDepthWorker::destroyContext() {
        if (!_display) {
            return;
        }
        EGLDisplay display = static_cast<EGLDisplay>(_display);
        if (_context) {
            if (_frameBufferId) {
                glDeleteFramebuffers(1, &_frameBufferId);
                glDeleteTextures(1, &_colorTextureId);
                glDeleteRenderbuffers(1, &_depthBufferId);
                _frameBufferId = _colorTextureId = _depthBufferId = 0;
            }
            if (_programId) {
                glDeleteProgram(_programId);
                _programId = 0;
            }
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, static_cast<EGLContext>(_context));
            _context = nullptr;
        }
        if (_surface) {
            eglDestroySurface(display, static_cast<EGLSurface>(_surface));
            _surface = nullptr;
        }
        eglTerminate(display);
        _display = nullptr;
    }

    std::shared_ptr<TerrainDepthWorker::Result> TerrainDepthWorker::renderJob(const Job& job) {
        if (!initFrameBuffer(job.width, job.height)) {
            return std::shared_ptr<Result>();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, _frameBufferId);
        glViewport(0, 0, job.width, job.height);

        // Clear to 'sky': maximum depth, zero coverage - the same encoding the synchronous
        // path writes, so both are sampled the same way.
        glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); // displaced surfaces can face away near ridge crests

        glUseProgram(_programId);
        glEnableVertexAttribArray(_aCoord);
        if (_uFar >= 0) {
            glUniform1f(_uFar, job.far);
        }
        // Straight out of the meshes, as the render thread draws them: uploading them into
        // buffers of the worker's own instead was measured to change nothing here (the cost of a
        // job is the context switch, not the vertex data).
        for (const DrawItem& item : job.items) {
            glUniformMatrix4fv(_uMVPMat, 1, GL_FALSE, item.mvpMat.data());
            glVertexAttribPointer(_aCoord, 3, GL_FLOAT, GL_FALSE, 0, item.vertices);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(item.indexCount), GL_UNSIGNED_SHORT, item.indices);
        }
        glDisableVertexAttribArray(_aCoord);

        auto result = std::make_shared<Result>();
        result->width = job.width;
        result->height = job.height;
        result->far = job.far;
        result->mvpMatrix = job.mvpMatrix;
        result->data.resize(static_cast<std::size_t>(job.width) * job.height * 4);
        glReadPixels(0, 0, job.width, job.height, GL_RGBA, GL_UNSIGNED_BYTE, result->data.data());
        return result;
    }

#else

    bool TerrainDepthWorker::isSupported() {
        return false;
    }

    int TerrainDepthWorker::getMovingSubmitInterval(int defaultInterval) {
        return defaultInterval;
    }

    bool TerrainDepthWorker::initContext() {
        return false;
    }

    bool TerrainDepthWorker::initProgram() {
        return false;
    }

    bool TerrainDepthWorker::initFrameBuffer(int width, int height) {
        return false;
    }

    void TerrainDepthWorker::destroyContext() {
    }

    std::shared_ptr<TerrainDepthWorker::Result> TerrainDepthWorker::renderJob(const Job& job) {
        return std::shared_ptr<Result>();
    }

#endif

    TerrainDepthWorker::TerrainDepthWorker(std::string vertexShaderSource, std::string fragmentShaderSource) :
        _vertexShaderSource(std::move(vertexShaderSource)),
        _fragmentShaderSource(std::move(fragmentShaderSource))
    {
        if (!isSupported()) {
            _unusable = true;
            return;
        }
        _thread = std::thread(&TerrainDepthWorker::threadLoop, this);
    }

    TerrainDepthWorker::~TerrainDepthWorker() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stopped = true;
        }
        _condition.notify_all();
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    bool TerrainDepthWorker::isUsable() const {
        return !_unusable;
    }

    bool TerrainDepthWorker::isBusy() const {
        return _busy;
    }

    bool TerrainDepthWorker::submit(Job job) {
        if (_unusable || _busy) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stopped || _pendingJob) {
                return false;
            }
            _busy = true;
            _pendingJob = std::make_unique<Job>(std::move(job));
        }
        _condition.notify_all();
        return true;
    }

    std::shared_ptr<const TerrainDepthWorker::Result> TerrainDepthWorker::takeResult() {
        std::lock_guard<std::mutex> lock(_mutex);
        std::shared_ptr<const Result> result = _result;
        _result.reset();
        return result;
    }

    void TerrainDepthWorker::threadLoop() {
        if (!initContext()) {
            Log::Info("TerrainDepthWorker: could not create an offscreen GL context, terrain occlusion depth stays on the render thread");
            destroyContext();
            _unusable = true;
            _busy = false;
            return;
        }

        while (true) {
            std::unique_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _condition.wait(lock, [this] { return _stopped || _pendingJob != nullptr; });
                if (_stopped) {
                    break;
                }
                job = std::move(_pendingJob);
                _pendingJob.reset();
            }

            std::shared_ptr<Result> result = renderJob(*job);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (result) {
                    _result = result;
                }
            }
            // The job (and with it the meshes it keeps alive) is released before the next wait,
            // so an idle map does not pin a screenful of terrain meshes.
            job.reset();
            _busy = false;
        }

        destroyContext();
        _busy = false;
    }

}
