#include "GLContext.h"
#include "utils/Log.h"
#include "utils/GeneralUtils.h"

#include <cstdlib>

namespace massif {

    std::atomic<GLContext::CurrentContextProvider> GLContext::_CurrentContextProvider(nullptr);

    void GLContext::SetCurrentContextProvider(CurrentContextProvider provider) {
        _CurrentContextProvider = provider;
    }

    void* GLContext::GetCurrentContext() {
        CurrentContextProvider provider = _CurrentContextProvider;
        return provider ? provider() : nullptr;
    }

    bool GLContext::HasGLExtension(const char* extension) {
        std::lock_guard<std::recursive_mutex> lock(_Mutex);
    
        auto it = _ExtensionCache.find(extension);
        if (it != _ExtensionCache.end()) {
            return true;
        }
        return false;
    }
    
    void GLContext::LoadExtensions() {
        std::lock_guard<std::recursive_mutex> lock(_Mutex);

        const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (!extensions) {
            return;
        }
    
        std::vector<std::string> tokens = GeneralUtils::Split(std::string(extensions), ' ');
        for (const std::string& extension : tokens) {
            _ExtensionCache.insert(extension);
        }
        
        TEXTURE_FILTER_ANISOTROPIC = HasGLExtension("GL_EXT_texture_filter_anisotropic");

        // Parsed the way tangram does it (Hardware::loadCapabilities): first digit run in
        // GL_VERSION, scaled by 100. "OpenGL ES 3.0 ..." -> 300.
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        for (const char* s = version ? version : ""; *s; ++s) {
            if (*s >= '0' && *s <= '9') {
                VERSION = static_cast<int>(std::strtof(s, nullptr) * 100.0f + 0.5f);
                break;
            }
        }
        Log::Infof("GLContext::LoadExtensions: %s (version %d), anisotropic filtering %d", version ? version : "?", VERSION, TEXTURE_FILTER_ANISOTROPIC ? 1 : 0);
        if (VERSION < 300) {
            // Not fatal here - the context was already created, and failing to draw is worse than
            // drawing wrongly. It tells a bug report why everything after this looks broken.
            Log::Errorf("GLContext::LoadExtensions: the SDK requires an OpenGL ES 3.0 context, got '%s'", version ? version : "?");
        }
    }
        
    void GLContext::CheckGLError(const char* place) {
        for (GLint error = glGetError(); error; error = glGetError()) {
            Log::Errorf("GLContext::CheckGLError: GLError (0x%x) at %s \n", error, place);
        }
    }

    void GLContext::InvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum* attachments) {
        glInvalidateFramebuffer(target, numAttachments, attachments);
    }

    GLContext::GLContext() {
    }

    int GLContext::VERSION = 0;

    bool GLContext::TEXTURE_FILTER_ANISOTROPIC = false;

    std::size_t GLContext::MAX_VERTEXBUFFER_SIZE = 65535; // Should NOT exceed 64k!

    std::unordered_set<std::string> GLContext::_ExtensionCache;
        
    std::recursive_mutex GLContext::_Mutex;
    
}
