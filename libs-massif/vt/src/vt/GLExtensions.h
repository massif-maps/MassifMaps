/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_GLEXTENSIONS_H_
#define _MASSIF_VT_GLEXTENSIONS_H_

#include <string>

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

namespace massif::vt {
    // What is left after the ES 3.0 baseline: anisotropic filtering is an extension in every ES
    // version, so it is the only thing still worth asking about. VAOs, standard derivatives,
    // packed depth-stencil and framebuffer invalidation are all core and are called directly.
    class GLExtensions final {
    public:
        GLExtensions();

        bool GL_EXT_texture_filter_anisotropic_supported() const { return _GL_EXT_texture_filter_anisotropic_supported; }

    private:
        bool _GL_EXT_texture_filter_anisotropic_supported = false;
    };
}

#endif
