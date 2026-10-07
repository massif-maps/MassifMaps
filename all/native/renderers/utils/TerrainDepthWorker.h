/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINDEPTHWORKER_H_
#define _MASSIF_TERRAINDEPTHWORKER_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cglib/mat.h>

namespace massif {

    /**
     * One read-back of the packed terrain depth (RGB = linear eye depth / far, A = coverage) at
     * BUFFER_DOWNSCALE resolution. Immutable once published, for the label placement worker.
     */
    struct TerrainDepthBuffer {
        std::vector<std::uint8_t> data;
        int width = 0;
        int height = 0;
        float far = 0;
        // Occlusion queries must project with this matrix: the buffer lags a moving camera.
        cglib::mat4x4<double> mvpMatrix = cglib::mat4x4<double>::zero();
        // The terrain it shows: during a 2D/3D ramp the ground has moved since, and anchors with it.
        float exaggeration = -1;
    };

    /**
     * Renders and reads back the terrain occlusion depth on its own thread and unshared EGL context, so the
     * glReadPixels stall stays off the render thread. EGL only (Android, ANGLE); elsewhere isSupported()
     * is false. See docs/internals/rendering/04-terrain.md. Internal class.
     */
    class TerrainDepthWorker {
    public:
        struct DrawItem {
            cglib::mat4x4<float> mvpMat;
            std::shared_ptr<const void> owner; // keeps the mesh data alive for as long as the job runs
            const float* vertices = nullptr;
            const std::uint16_t* indices = nullptr;
            std::size_t indexCount = 0;
        };

        struct Job {
            int width = 0;
            int height = 0;
            float far = 0;
            cglib::mat4x4<double> mvpMatrix = cglib::mat4x4<double>::zero(); // carried into the result
            float exaggeration = -1; // carried into the result
            std::vector<DrawItem> items;
        };

        using Result = TerrainDepthBuffer;

        TerrainDepthWorker(std::string vertexShaderSource, std::string fragmentShaderSource);
        virtual ~TerrainDepthWorker();

        /**
         * False without an offscreen GL context; the caller then reads back on the render thread.
         */
        static bool isSupported();

        /**
         * Minimum interval (ms) between jobs while the camera moves, so the two contexts do not contend
         * every frame. Demo builds: 'adb shell setprop debug.massif.asyncdepthms N' overrides it.
         */
        static int getMovingSubmitInterval(int defaultInterval);

        /**
         * False once the offscreen context failed; settles only after the first job. The caller must then
         * go back to the synchronous path.
         */
        bool isUsable() const;

        /**
         * True while a job is being rendered or read back; submitting before it clears is pointless.
         */
        bool isBusy() const;

        /**
         * Never blocks or touches the render context. Returns false, taking nothing, when busy or unusable.
         */
        bool submit(Job job);

        /**
         * The most recently finished result, or null when nothing finished since the last call.
         */
        std::shared_ptr<const Result> takeResult();

    private:
        void threadLoop();
        bool initContext();
        void destroyContext();
        bool initFrameBuffer(int width, int height);
        bool initProgram();
        std::shared_ptr<Result> renderJob(const Job& job);

        const std::string _vertexShaderSource;
        const std::string _fragmentShaderSource;

        // Opaque to keep GL headers out; worker thread only.
        void* _display = nullptr;
        void* _context = nullptr;
        void* _surface = nullptr;
        unsigned int _frameBufferId = 0;
        unsigned int _colorTextureId = 0;
        unsigned int _depthBufferId = 0;
        unsigned int _programId = 0;
        int _aCoord = -1;
        int _uMVPMat = -1;
        int _uFar = -1;
        int _frameBufferWidth = 0;
        int _frameBufferHeight = 0;

        mutable std::mutex _mutex;
        std::condition_variable _condition;
        std::thread _thread;
        std::unique_ptr<Job> _pendingJob;
        std::shared_ptr<const Result> _result;
        std::atomic<bool> _busy = { false };
        std::atomic<bool> _unusable = { false };
        bool _stopped = false;
    };

}

#endif
