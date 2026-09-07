#include "TerrainShadowMap.h"
#include "renderers/utils/GLContext.h"
#include "utils/Log.h"

#include <algorithm>

namespace massif {

    TerrainShadowMap::TerrainShadowMap() :
        _size(1024),
        _cascades(1),
        _frameBuffer(0),
        _texture(0),
        _depthBuffer(0),
        // ES 3.0 core, so it starts on; the incomplete-framebuffer fallback below is the only
        // thing that clears it, and it stays cleared for the size retries that follow.
        _depthTextureMode(true),
        _hardwarePCF(false),
        _failed(false)
    {
    }

    TerrainShadowMap::~TerrainShadowMap() {
        // GL resources must be released explicitly via deleteResources() while the context is
        // current; the destructor may run after it is gone.
    }

    int TerrainShadowMap::getSize() const {
        return _size;
    }

    int TerrainShadowMap::getCascades() const {
        return _cascades;
    }

    void TerrainShadowMap::setSize(int size, int cascades) {
        int clampedCascades = std::min(MAX_CASCADES, std::max(1, cascades));
        // The pages sit side by side in ONE texture, so the widest supported texture caps the
        // per-cascade resolution. Ask the driver rather than assume 4096: that silently capped
        // 3 x 2048 at 3 x 1365 on hardware that would have taken it.
        static int maxTextureSize = 0;
        if (maxTextureSize == 0) {
            GLint value = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
            // Bounded by memory as well as by the driver's limit: the atlas is RGBA plus a depth
            // renderbuffer of the same size, so a 4-page 8192 atlas would be a third of a gigabyte.
            maxTextureSize = std::min(value >= 2048 ? value : 4096, 8192);
        }
        int clamped = std::min(maxTextureSize / clampedCascades, std::max(256, size));
        if (clamped != _size || clampedCascades != _cascades) {
            _size = clamped;
            _cascades = clampedCascades;
            deleteResources();
            _failed = false;
        }
    }

    bool TerrainShadowMap::createResources() {
        if (_frameBuffer != 0) {
            return true;
        }
        if (_failed) {
            return false;
        }
        // A size the driver will not allocate must not mean "no shadows, ever". Halve and retry
        // down to 256 - a smaller map is worth far more than a blank one, and without this a single
        // over-large setShadowMapSize turned the shadows off for the rest of the session, silently.
        while (!createResourcesAtSize() && _size > 256) {
            int reduced = std::max(256, _size / 2);
            Log::Warnf("TerrainShadowMap: %dx%d atlas (%d cascades) could not be created, retrying at %d", _size * _cascades, _size, _cascades, reduced);
            _size = reduced;
            _failed = false;
        }
        if (_failed) {
            Log::Errorf("TerrainShadowMap: no usable shadow atlas at %d x %d cascades - shadows are off", _size, _cascades);
        }
        return !_failed;
    }

    bool TerrainShadowMap::createResourcesAtSize() {
        // The DEPTH BUFFER IS THE MAP: the caster pass writes depth alone rather than depth plus a
        // packed-RGB copy, so the atlas is 16 bits instead of 32 + 16. ES 3.0 core, so the only way
        // back to the packed-colour path is the incomplete-framebuffer fallback below.

        glGenTextures(1, &_texture);
        glBindTexture(GL_TEXTURE_2D, _texture);
        if (_depthTextureMode) {
            // 24 bits. The packed path stored gl_FragCoord.z across three bytes, so a 16-bit depth
            // texture would LOSE precision and buy acne back.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, _size * _cascades, _size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _size * _cascades, _size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        // A COMPARISON sampler: the unit does four depth compares per fetch and returns their
        // bilinear average, so LINEAR is right. On the packed-colour fallback it must be NEAREST -
        // interpolating two depths gives a third, meaningless one.
        _hardwarePCF = _depthTextureMode;
        if (_hardwarePCF) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, _hardwarePCF ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, _hardwarePCF ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (!_depthTextureMode) {
            glGenRenderbuffers(1, &_depthBuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, _depthBuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, _size * _cascades, _size);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }

        GLint prevFrameBuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFrameBuffer);
        glGenFramebuffers(1, &_frameBuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
        if (_depthTextureMode) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _texture, 0);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texture, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthBuffer);
        }
        bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, prevFrameBuffer);
        if (!complete) {
            // A depth-only framebuffer is complete by the ES3 spec, but a driver that refuses one
            // must not mean no shadows: fall back to the packed target rather than to nothing.
            bool retryPacked = _depthTextureMode;
            deleteResources();
            if (retryPacked) {
                Log::Warn("TerrainShadowMap: depth-only framebuffer incomplete, falling back to the packed-colour map");
                _depthTextureMode = false;
                return createResourcesAtSize();
            }
            _failed = true;
            return false;
        }
        return true;
    }

    unsigned int TerrainShadowMap::getTexture() {
        return createResources() ? _texture : 0;
    }

    bool TerrainShadowMap::beginPass(bool clearAll) {
        if (!createResources()) {
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
        // Re-attached per pass; endPass detaches it. A texture left attached to a framebuffer is
        // still a render target, and sampling one in the same frame - which every shadowed draw
        // does - is undefined and serialises on this driver.
        if (_depthTextureMode) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _texture, 0);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texture, 0);
        }
        glViewport(0, 0, _size * _cascades, _size);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE); // terrain surfaces can face away from the sun near ridge crests
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        // Slope-scaled offset on the CASTER, the only bias that works here: one shadow texel covers
        // tens of metres, so at a grazing angle the depth varies across a texel by more than any
        // constant bias absorbs - and one large enough would detach the shadows from their ridges.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);
        // White = depth 1 = nothing in the way, which is what an untouched texel must mean.
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        // Nothing is written to colour in depth-texture mode - there is no colour attachment - and
        // masking it also spares the caster fragment shader's packing every fragment.
        glColorMask(_depthTextureMode ? GL_FALSE : GL_TRUE, _depthTextureMode ? GL_FALSE : GL_TRUE, _depthTextureMode ? GL_FALSE : GL_TRUE, _depthTextureMode ? GL_FALSE : GL_TRUE);
        if (clearAll) {
            glDisable(GL_SCISSOR_TEST);
            glClear(clearMask());
        }
        return true;
    }

    void TerrainShadowMap::setCascadeViewport(int cascade) {
        int index = std::min(_cascades - 1, std::max(0, cascade));
        glViewport(index * _size, 0, _size, _size);
        // Scissored as well as viewported: the pages are refreshed independently, so a clear for
        // one of them must not blank the ones being reused.
        glScissor(index * _size, 0, _size, _size);
        glEnable(GL_SCISSOR_TEST);
    }

    void TerrainShadowMap::clearCascade() {
        glClear(clearMask());
    }

    unsigned int TerrainShadowMap::clearMask() const {
        return _depthTextureMode ? GL_DEPTH_BUFFER_BIT : (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    bool TerrainShadowMap::isDepthTexture() const {
        return _depthTextureMode;
    }

    bool TerrainShadowMap::isHardwarePCF() const {
        return _hardwarePCF;
    }

    void TerrainShadowMap::endPass(unsigned int previousFrameBuffer, int viewportWidth, int viewportHeight) {
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, _depthTextureMode ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0); // see beginPass
        glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, 0.0f);
        glBindFramebuffer(GL_FRAMEBUFFER, previousFrameBuffer);
        glViewport(0, 0, viewportWidth, viewportHeight);
        glEnable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
    }

    void TerrainShadowMap::deleteResources() {
        if (_frameBuffer != 0) {
            glDeleteFramebuffers(1, &_frameBuffer);
            _frameBuffer = 0;
        }
        if (_texture != 0) {
            glDeleteTextures(1, &_texture);
            _texture = 0;
        }
        if (_depthBuffer != 0) {
            glDeleteRenderbuffers(1, &_depthBuffer);
            _depthBuffer = 0;
        }
    }

}
