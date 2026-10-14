#include "GLResourceManager.h"
#include "utils/Log.h"

namespace massif {

    GLResourceManager::GLResourceManager() :
        _glThreadId(),
        _glContext(nullptr),
        _deferredWrongContext(0),
        _createQueue(),
        _deleteQueue(),
        _mutex()
    {
    }
    
    GLResourceManager::~GLResourceManager() {
        if (isOnGLContext()) {
            for (const std::unique_ptr<GLResource>& resource : _deleteQueue) {
                resource->destroy();
            }
            _deleteQueue.clear();
        }
        if (!_deleteQueue.empty()) {
            Log::Debugf("GLResourceManager::~GLResourceManager: Delete queue size: %d", static_cast<int>(_deleteQueue.size()));
        }
    }

    std::thread::id GLResourceManager::getGLThreadId() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _glThreadId;
    }
    
    void GLResourceManager::setGLThreadId(std::thread::id id) {
        std::lock_guard<std::mutex> lock(_mutex);
        _glThreadId = id;
    }
    
    void GLResourceManager::setGLContext(void* context) {
        std::lock_guard<std::mutex> lock(_mutex);
        _glContext = context;
    }

    bool GLResourceManager::isOnGLContext() const {
        void* context = nullptr;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (std::this_thread::get_id() != _glThreadId) {
                return false;
            }
            context = _glContext;
        }
        return !context || context == GLContext::GetCurrentContext();
    }

    void GLResourceManager::processResources() {
        if (std::this_thread::get_id() != getGLThreadId()) {
            Log::Warn("GLResourceManager::processResources: Method called from wrong thread!");
            return;
        }
        if (!isOnGLContext()) {
            Log::Warn("GLResourceManager::processResources: Method called with another GL context current!");
            return;
        }
        if (int deferred = _deferredWrongContext.exchange(0)) {
            Log::Debugf("GLResourceManager::processResources: %d resources were released/created off-context and deferred to this context", deferred);
        }

        std::vector<std::weak_ptr<GLResource> > createQueue;
        std::vector<std::unique_ptr<GLResource> > deleteQueue;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            std::swap(deleteQueue, _deleteQueue);
            std::swap(createQueue, _createQueue);
        }
        for (const std::unique_ptr<GLResource>& resource : deleteQueue) {
            resource->destroy();
        }
        for (const std::weak_ptr<GLResource>& resourceWeak : createQueue) {
            if (auto resource = resourceWeak.lock()) {
                resource->create();
            }
        }
    }

    std::shared_ptr<GLResource> GLResourceManager::registerResource(GLResource* resourcePtr) {
        std::shared_ptr<GLResource> resource;
        try {
            std::shared_ptr<GLResourceManager> manager(shared_from_this());
            resource = std::shared_ptr<GLResource>(resourcePtr, [manager](GLResource* resourcePtr) {
                manager->deleteResource(std::unique_ptr<GLResource>(resourcePtr));
            });
        }
        catch (const std::exception&) {
            delete resourcePtr;
            throw;
        }

        if (isOnGLContext()) {
            resource->create();
        } else {
            if (std::this_thread::get_id() == getGLThreadId()) {
                _deferredWrongContext++;
            }
            std::lock_guard<std::mutex> lock(_mutex);
            _createQueue.push_back(resource);
        }
        return resource;
    }

    void GLResourceManager::deleteResource(std::unique_ptr<GLResource> resource) {
        if (resource) {
            if (isOnGLContext()) {
                resource->destroy();
            } else {
                if (std::this_thread::get_id() == getGLThreadId()) {
                    _deferredWrongContext++;
                }
                std::lock_guard<std::mutex> lock(_mutex);
                _deleteQueue.push_back(std::move(resource));
            }
        }
    }

}
