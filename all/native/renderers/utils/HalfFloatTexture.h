/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_HALFFLOATTEXTURE_H_
#define _MASSIF_HALFFLOATTEXTURE_H_

#include "renderers/utils/GLResource.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace massif {

    /**
     * A two-channel half float texture (RG16F), linear filtered and clamped to edge. The data is released once
     * uploaded. Internal class.
     */
    class HalfFloatTexture : public GLResource {
    public:
        virtual ~HalfFloatTexture();

        GLuint getTexId() const;

        // Rows packed, two halves per texel; no-op before creation.
        void updateSubImage(int x, int y, int width, int height, const std::uint16_t* data);

    protected:
        friend GLResourceManager;

        HalfFloatTexture(const std::weak_ptr<GLResourceManager>& manager, int width, int height, std::vector<std::uint16_t> data);

        virtual void create();
        virtual void destroy();

    private:
        int _width;
        int _height;
        std::vector<std::uint16_t> _data;
        GLuint _texId;
    };

}

#endif
