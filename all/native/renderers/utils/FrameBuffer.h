/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FRAMEBUFFER_H_
#define _MASSIF_FRAMEBUFFER_H_

#include "renderers/utils/GLResource.h"

#include <memory>
#include <vector>

namespace massif {
    
    class FrameBuffer : public GLResource {
    public:
        virtual ~FrameBuffer();
        
        int getWidth() const;
        int getHeight() const;


        GLuint getFBOId() const;
        GLuint getColorTexId() const;
        /**
         * The color texture drawing currently goes to (the secondary one while attached).
         */
        GLuint getAttachedColorTexId() const;
        /**
         * Attaches the secondary color texture (created on first use) or the primary one. Depth/stencil
         * stay, so content drawn after a post-process effect is still occluded by the scene.
         */
        void attachSecondaryColorTex(bool secondary);

        void discard(bool color, bool depth, bool stencil);

    protected:
        friend GLResourceManager;

        FrameBuffer(const std::weak_ptr<GLResourceManager>& manager, int width, int height, bool color, bool depth, bool stencil);

        virtual void create();
        virtual void destroy();

    private:
        int _width;
        int _height;
        bool _color;
        bool _depth;
        bool _stencil;
    
        GLuint _fboId;
        GLuint _colorTexId;
        GLuint _secondaryColorTexId;
        bool _secondaryAttached;
        std::vector<GLuint> _depthStencilRBIds;
    };
    
}

#endif
