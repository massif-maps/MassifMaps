#include "GLExtensions.h"

#include <EGL/egl.h>

namespace massif::vt {
    GLExtensions::GLExtensions() {
        std::string paddedExtensions;
        const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (extensions) {
            paddedExtensions = " " + std::string(extensions) + " ";
        }

        // Vertex array objects are core in OpenGL ES 3.0, under the unsuffixed names and with
        // no extension string to advertise them. Without them every draw call re-specifies
        // each vertex attribute one by one - measured at 170-510 geometry draws per frame for
        // an ordinary style, i.e. thousands of redundant GL calls a frame.
        bool gles3 = false;
        if (const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION))) {
            std::string versionStr(version);
            std::size_t pos = versionStr.find("OpenGL ES ");
            if (pos != std::string::npos && pos + 10 < versionStr.size()) {
                gles3 = versionStr[pos + 10] >= '3';
            }
        }

#ifdef GL_OES_vertex_array_object
        if (gles3) {
            _glBindVertexArrayOES = reinterpret_cast<PFNGLBINDVERTEXARRAYOESPROC>(eglGetProcAddress("glBindVertexArray"));
            _glDeleteVertexArraysOES = reinterpret_cast<PFNGLDELETEVERTEXARRAYSOESPROC>(eglGetProcAddress("glDeleteVertexArrays"));
            _glGenVertexArraysOES = reinterpret_cast<PFNGLGENVERTEXARRAYSOESPROC>(eglGetProcAddress("glGenVertexArrays"));
            _glIsVertexArrayOES = reinterpret_cast<PFNGLISVERTEXARRAYOESPROC>(eglGetProcAddress("glIsVertexArray"));
            _GL_OES_vertex_array_object_supported = _glBindVertexArrayOES && _glDeleteVertexArraysOES && _glGenVertexArraysOES && _glIsVertexArrayOES;
        }
#ifdef __ANDROID__
        // ES 2 only: the OES extension stays off on Android - it is reported by
        // implementations where it does not actually work (a known Asus MemoPad case).
#else
        if (!_GL_OES_vertex_array_object_supported) {
            _GL_OES_vertex_array_object_supported = paddedExtensions.find(" GL_OES_vertex_array_object ") != std::string::npos;
            if (_GL_OES_vertex_array_object_supported) {
                _glBindVertexArrayOES = reinterpret_cast<PFNGLBINDVERTEXARRAYOESPROC>(eglGetProcAddress("glBindVertexArrayOES"));
                _glDeleteVertexArraysOES = reinterpret_cast<PFNGLDELETEVERTEXARRAYSOESPROC>(eglGetProcAddress("glDeleteVertexArraysOES"));
                _glGenVertexArraysOES = reinterpret_cast<PFNGLGENVERTEXARRAYSOESPROC>(eglGetProcAddress("glGenVertexArraysOES"));
                _glIsVertexArrayOES = reinterpret_cast<PFNGLISVERTEXARRAYOESPROC>(eglGetProcAddress("glIsVertexArrayOES"));
                _GL_OES_vertex_array_object_supported = _glBindVertexArrayOES && _glDeleteVertexArraysOES && _glGenVertexArraysOES && _glIsVertexArrayOES;
            }
        }
#endif
#endif

#ifdef GL_EXT_discard_framebuffer
        _GL_EXT_discard_framebuffer_supported = paddedExtensions.find(" GL_EXT_discard_framebuffer ") != std::string::npos;
        if (_GL_EXT_discard_framebuffer_supported) {
            _glDiscardFramebufferEXT = reinterpret_cast<PFNGLDISCARDFRAMEBUFFEREXTPROC>(eglGetProcAddress("glDiscardFramebufferEXT"));
        }
#endif

#ifdef GL_EXT_texture_filter_anisotropic
        _GL_EXT_texture_filter_anisotropic_supported = paddedExtensions.find(" GL_EXT_texture_filter_anisotropic ") != std::string::npos;
#endif

#ifdef GL_OES_packed_depth_stencil
        _GL_OES_packed_depth_stencil_supported = paddedExtensions.find(" GL_OES_packed_depth_stencil ") != std::string::npos;
#endif

#ifdef GL_OES_standard_derivatives
        _GL_OES_standard_derivatives_supported = paddedExtensions.find(" GL_OES_standard_derivatives ") != std::string::npos;
#endif
    }

    void GLExtensions::glBindVertexArrayOES(GLuint array) {
#ifdef GL_OES_vertex_array_object
        _glBindVertexArrayOES(array);
#endif
    }
    void GLExtensions::glDeleteVertexArraysOES(GLsizei n, const GLuint* arrays) {
#ifdef GL_OES_vertex_array_object
        _glDeleteVertexArraysOES(n, arrays);
#endif
    }

    void GLExtensions::glGenVertexArraysOES(GLsizei n, GLuint* arrays) {
#ifdef GL_OES_vertex_array_object
        _glGenVertexArraysOES(n, arrays);
#endif
    }

    GLboolean GLExtensions::glIsVertexArrayOES(GLuint array) {
#ifdef GL_OES_vertex_array_object
        return _glIsVertexArrayOES(array);
#endif
    }

    void GLExtensions::glDiscardFramebufferEXT(GLenum target, GLsizei numAttachments, const GLenum* attachments) {
#ifdef GL_EXT_discard_framebuffer
        return _glDiscardFramebufferEXT(target, numAttachments, attachments);
#endif
    }
}
