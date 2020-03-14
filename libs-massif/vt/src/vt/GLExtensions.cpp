#include "GLExtensions.h"

#include <EGL/egl.h>

namespace carto { namespace vt {
    GLExtensions::GLExtensions() {
        std::string paddedExtensions;
        const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (extensions) {
            paddedExtensions = " " + std::string(extensions) + " ";
        }

#ifdef GL_OES_vertex_array_object
#ifdef __ANDROID__
        _GL_OES_vertex_array_object_supported = false; // Android is a wild-wild west of GL implementations, at least one instance is known (Asus MemoPad) where extension support is reported but does not actually work
#else
        _GL_OES_vertex_array_object_supported = paddedExtensions.find(" GL_OES_vertex_array_object ") != std::string::npos;
#endif
        if (_GL_OES_vertex_array_object_supported) {
            _glBindVertexArrayOES = reinterpret_cast<PFNGLBINDVERTEXARRAYOESPROC>(eglGetProcAddress("glBindVertexArrayOES"));
            _glDeleteVertexArraysOES = reinterpret_cast<PFNGLDELETEVERTEXARRAYSOESPROC>(eglGetProcAddress("glDeleteVertexArraysOES"));
            _glGenVertexArraysOES = reinterpret_cast<PFNGLGENVERTEXARRAYSOESPROC>(eglGetProcAddress("glGenVertexArraysOES"));
            _glIsVertexArrayOES = reinterpret_cast<PFNGLISVERTEXARRAYOESPROC>(eglGetProcAddress("glIsVertexArrayOES"));
            _GL_OES_vertex_array_object_supported = _glBindVertexArrayOES && _glDeleteVertexArraysOES && _glGenVertexArraysOES && _glIsVertexArrayOES;
        }
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
} }
