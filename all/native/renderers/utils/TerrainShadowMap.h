/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINSHADOWMAP_H_
#define _MASSIF_TERRAINSHADOWMAP_H_

namespace massif {

    /**
     * Offscreen target for the directional shadow caster pass: a depth texture, or light-space depth packed
     * into RGB as a fallback. Casters use the on-screen vertex shader, so self-shadowing has no
     * acne from a mismatched mesh. GL thread only.
     */
    class TerrainShadowMap {
    public:
        // Must match the cascade count the vt shaders declare (vt::GLTileRenderer::MAX_SHADOW_CASCADES).
        static constexpr int MAX_CASCADES = 4;

        TerrainShadowMap();
        ~TerrainShadowMap();

        int getSize() const;
        int getCascades() const;
        /**
         * Sets the resolution and cascade count. Cascades are side-by-side pages of one texture, nearest
         * first, so one sampler reaches them all. A change drops the resources.
         */
        void setSize(int size, int cascades);

        /**
         * Returns the shadow texture, created on first use, or 0 when the framebuffer is incomplete.
         */
        unsigned int getTexture();
        /**
         * Binds the framebuffer, clearing every page to "infinitely far" when clearAll is set.
         * Returns false if unavailable.
         */
        bool beginPass(bool clearAll);
        /**
         * Restricts drawing (and clearing) to one cascade's page. Must be called after beginPass.
         */
        void setCascadeViewport(int cascade);
        /**
         * Clears the current cascade's page alone, for a pass that refreshes only some of them.
         */
        void clearCascade();
        /**
         * Restores the previous framebuffer and viewport.
         */
        void endPass(unsigned int previousFrameBuffer, int viewportWidth, int viewportHeight);

        /**
         * True for a sampled depth texture rather than packed RGB; selects the receiver shaders' lookup.
         */
        bool isDepthTexture() const;

        /**
         * True when bound as a comparison sampler (one fetch = 4 bilinear-averaged compares); selects the
         * receiver shaders' lookup and version.
         */
        bool isHardwarePCF() const;

        /**
         * Deletes all GL resources. Must be called on the GL thread while the context is alive.
         */
        void deleteResources();

    private:
        bool createResources();
        bool createResourcesAtSize();
        unsigned int clearMask() const;

        int _size;
        int _cascades;
        unsigned int _frameBuffer;
        unsigned int _texture;
        unsigned int _depthBuffer;
        bool _depthTextureMode;
        bool _hardwarePCF;
        bool _failed;
    };

}

#endif
