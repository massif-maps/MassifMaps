/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_SCREENMASKBUFFER_H_
#define _MASSIF_SCREENMASKBUFFER_H_

namespace massif {

    /**
     * One-channel screen-space mask at a fraction of screen resolution, cleared to white: resolves a value
     * (terrain shadow, contact-shadow MIN) once per pixel instead of per draw. Penumbras hide the low
     * resolution; LINEAR filtering hides the texels. GL thread only.
     */
    class ScreenMaskBuffer {
    public:
        /** useDepth: attach a depth buffer, for a pass whose draws overlap in depth. */
        explicit ScreenMaskBuffer(bool useDepth = true);
        ~ScreenMaskBuffer();

        int getWidth() const;
        int getHeight() const;

        /**
         * Sets the screen size and the divisor the mask is rendered at; a size change drops the resources.
         */
        void setSize(int screenWidth, int screenHeight, int divisor);

        /**
         * Returns the mask texture, created on first use, or 0 when the framebuffer is incomplete.
         */
        unsigned int getTexture();
        /**
         * Binds the framebuffer and clears it to "fully lit". Returns false if unavailable.
         */
        bool beginPass();
        /**
         * Restores the previous framebuffer and viewport.
         */
        void endPass(unsigned int previousFrameBuffer, int viewportWidth, int viewportHeight);

        /**
         * beginPass without touching blend, cull or depth, for a pass that owns its render state
         * (the drape bake needs culling off, or every later baked tile comes out empty).
         */
        bool beginPassRaw();
        void endPassRaw(unsigned int previousFrameBuffer, int viewportWidth, int viewportHeight);

        /**
         * Deletes all GL resources. Must be called on the GL thread while the context is alive.
         */
        void deleteResources();

    private:
        bool createResources();

        const bool _useDepth;
        int _width;
        int _height;
        unsigned int _frameBuffer;
        unsigned int _texture;
        unsigned int _depthBuffer;
        bool _failed;
    };

}

#endif
