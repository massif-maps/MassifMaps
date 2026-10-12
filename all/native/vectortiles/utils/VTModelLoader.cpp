#include "VTModelLoader.h"
#include "VTBitmapLoader.h"
#include "core/BinaryData.h"
#include "utils/AssetPackage.h"
#include "utils/FileUtils.h"
#include "utils/Log.h"

#include <vt/GLBParser.h>

namespace massif {

    VTModelLoader::VTModelLoader(const std::string& basePath, const std::shared_ptr<AssetPackage>& assetPackage) :
        _basePath(basePath),
        _assetPackage(assetPackage),
        _urlFileLoader()
    {
        _urlFileLoader.setCaching(true);
    }

    std::shared_ptr<const vt::Model> VTModelLoader::load(const std::string& url) const {
        std::shared_ptr<BinaryData> fileData;
        if (_urlFileLoader.isSupported(url)) {
            if (!_urlFileLoader.load(url, fileData)) {
                Log::Errorf("VTModelLoader: Failed to load model: %s", url.c_str());
            }
        } else {
            std::string fileName = FileUtils::NormalizePath(_basePath + url);
            if (_assetPackage) {
                fileData = _assetPackage->loadAsset(fileName);
            }
            if (!fileData) {
                Log::Errorf("VTModelLoader: Failed to load model from asset package: %s", fileName.c_str());
            }
        }
        if (!fileData) {
            return std::shared_ptr<const vt::Model>();
        }

        std::string error;
        std::shared_ptr<const vt::Model> model = vt::parseGLB(fileData->data(), fileData->size(), &VTBitmapLoader::DecodeBitmap, error);
        if (!model) {
            Log::Errorf("VTModelLoader: Failed to parse model %s: %s", url.c_str(), error.c_str());
        }
        return model;
    }

}
