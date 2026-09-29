#include "HalfFloatTexture.h"
#include "renderers/utils/GLResourceManager.h"

namespace massif {

    HalfFloatTexture::~HalfFloatTexture() {
    }

    GLuint HalfFloatTexture::getTexId() const {
        return _texId;
    }

    HalfFloatTexture::HalfFloatTexture(const std::weak_ptr<GLResourceManager>& manager, int width, int height, std::vector<std::uint16_t> data) :
        GLResource(manager),
        _width(width),
        _height(height),
        _data(std::move(data)),
        _texId(0)
    {
    }

    void HalfFloatTexture::updateSubImage(int x, int y, int width, int height, const std::uint16_t* data) {
        if (_texId == 0 || !data || width <= 0 || height <= 0) {
            return;
        }
        GLint oldTexId = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexId);
        glBindTexture(GL_TEXTURE_2D, _texId);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RG, GL_HALF_FLOAT, data);
        glBindTexture(GL_TEXTURE_2D, oldTexId);

        GLContext::CheckGLError("HalfFloatTexture::updateSubImage");
    }

    void HalfFloatTexture::create() {
        if (_texId != 0 || _data.size() < static_cast<std::size_t>(_width) * _height * 2) {
            return;
        }
        GLint oldTexId = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexId);

        glGenTextures(1, &_texId);
        glBindTexture(GL_TEXTURE_2D, _texId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, _width, _height, 0, GL_RG, GL_HALF_FLOAT, _data.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, oldTexId);

        std::vector<std::uint16_t>().swap(_data);

        GLContext::CheckGLError("HalfFloatTexture::create");
    }

    void HalfFloatTexture::destroy() {
        if (_texId != 0) {
            glDeleteTextures(1, &_texId);
            _texId = 0;

            GLContext::CheckGLError("HalfFloatTexture::destroy");
        }
    }

}
