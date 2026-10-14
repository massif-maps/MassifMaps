/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_GLRESOURCEMANAGER_H_
#define _MASSIF_GLRESOURCEMANAGER_H_

#include "renderers/utils/GLResource.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace massif {

    class GLResourceManager : public std::enable_shared_from_this<GLResourceManager> {
    public:
        GLResourceManager();
        virtual ~GLResourceManager();

        std::thread::id getGLThreadId() const;
        void setGLThreadId(std::thread::id id);
        // The context the resources live in; nullptr when the platform cannot tell contexts apart.
        void setGLContext(void* context);

        template <typename T, typename... Args>
        std::shared_ptr<T> create(Args&&... args) {
            std::weak_ptr<GLResourceManager> managerWeak(shared_from_this());
            return std::static_pointer_cast<T>(registerResource(new T(managerWeak, std::forward<Args>(args)...)));
        }
    
        void processResources();
    
    protected:
        std::shared_ptr<GLResource> registerResource(GLResource* resourcePtr);
        void deleteResource(std::unique_ptr<GLResource> resource);

    private:
        // Right thread is not enough when several views share it: GL names are per context.
        bool isOnGLContext() const;

        std::thread::id _glThreadId;
        void* _glContext;
        std::atomic<int> _deferredWrongContext;
        std::vector<std::weak_ptr<GLResource> > _createQueue;
        std::vector<std::unique_ptr<GLResource> > _deleteQueue;
        mutable std::mutex _mutex;
    };
    
}

#endif
