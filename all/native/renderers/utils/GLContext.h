/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_GLCONTEXT_H_
#define _MASSIF_GLCONTEXT_H_

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <mutex>
#include <string>
#include <unordered_set>
#include <atomic>

namespace massif {

    class GLContext {
    public:
        // GL_VERSION as an integer, tangram's shape (core/src/gl/hardware.cpp): 300 for OpenGL
        // ES 3.0. The SDK requires >= 300, so this is for logging and for a future 3.1/3.2 gate,
        // not for deciding whether a core feature exists.
        static int VERSION;

        // The one capability still worth probing: anisotropic filtering is an extension in every
        // ES version, not core. Everything else this class used to ask about - NPOT, VAOs,
        // packed depth-stencil, depth textures, framebuffer invalidation - is ES 3.0 core.
        static bool TEXTURE_FILTER_ANISOTROPIC;

        static std::size_t MAX_VERTEXBUFFER_SIZE;

        static bool HasGLExtension(const char* extension);

        static void LoadExtensions();

        static void CheckGLError(const char* place);

        // Identity of the GL context current on the calling thread, nullptr when the platform
        // registered no provider. Only needed where several views share one thread (iOS).
        typedef void* (*CurrentContextProvider)();
        static void SetCurrentContextProvider(CurrentContextProvider provider);
        static void* GetCurrentContext();

        static void InvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum* attachments);

    private:
        GLContext();

        static std::unordered_set<std::string> _ExtensionCache;

        static std::recursive_mutex _Mutex;

        static std::atomic<CurrentContextProvider> _CurrentContextProvider;
    };

}

#endif
