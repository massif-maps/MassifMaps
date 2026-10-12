#ifndef _MASSIF_VTMODELLOADER_H_
#define _MASSIF_VTMODELLOADER_H_

#include "utils/URLFileLoader.h"

#include <memory>
#include <string>

#include <vt/ModelManager.h>

namespace massif {
    class AssetPackage;

    // Fetches a .glb the way VTBitmapLoader fetches an image: a URL, else a file of the style's asset package.
    class VTModelLoader : public vt::ModelManager::ModelLoader {
    public:
        VTModelLoader(const std::string& basePath, const std::shared_ptr<AssetPackage>& assetPackage);

        virtual std::shared_ptr<const vt::Model> load(const std::string& url) const;

    private:
        std::string _basePath;
        std::shared_ptr<AssetPackage> _assetPackage;
        URLFileLoader _urlFileLoader;
    };
}

#endif
