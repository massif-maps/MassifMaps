#ifndef _MASSIF_VT_MODELMANAGER_H_
#define _MASSIF_VT_MODELMANAGER_H_

#include "Model.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace massif::vt {
    class ModelManager final {
    public:
        class ModelLoader {
        public:
            virtual ~ModelLoader() = default;

            virtual std::shared_ptr<const Model> load(const std::string& fileName) const = 0;
        };

        explicit ModelManager(std::shared_ptr<ModelLoader> loader) : _modelLoader(std::move(loader)) { }

        // A failed load is cached too, so a missing file is fetched once rather than per tile.
        std::shared_ptr<const Model> loadModel(const std::string& fileName) {
            std::lock_guard<std::mutex> lock(_mutex);

            auto it = _modelMap.find(fileName);
            if (it != _modelMap.end()) {
                return it->second;
            }
            std::shared_ptr<const Model> model = _modelLoader ? _modelLoader->load(fileName) : std::shared_ptr<const Model>();
            _modelMap[fileName] = model;
            return model;
        }

    private:
        const std::shared_ptr<ModelLoader> _modelLoader;
        std::map<std::string, std::shared_ptr<const Model>> _modelMap;
        mutable std::mutex _mutex;
    };
}

#endif
