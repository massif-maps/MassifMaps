#include "GLExtensions.h"

namespace massif::vt {
    GLExtensions::GLExtensions() {
        std::string paddedExtensions;
        const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (extensions) {
            paddedExtensions = " " + std::string(extensions) + " ";
        }

#ifdef GL_EXT_texture_filter_anisotropic
        _GL_EXT_texture_filter_anisotropic_supported = paddedExtensions.find(" GL_EXT_texture_filter_anisotropic ") != std::string::npos;
#endif
    }
}
