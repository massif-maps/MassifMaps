#include "BundleAssetPackage.h"
#include "core/BinaryData.h"
#include "utils/AssetUtils.h"
#include "utils/FileUtils.h"

#include <algorithm>

namespace massif {

    BundleAssetPackage::BundleAssetPackage(const std::string& basePath) :
        _basePath(FileUtils::NormalizePath(basePath)),
        _baseAssetPackage(),
        _localAssetNames(),
        _localAssetNamesValid(false)
    {
    }

    BundleAssetPackage::BundleAssetPackage(const std::string& basePath, const std::shared_ptr<AssetPackage>& baseAssetPackage) :
        _basePath(FileUtils::NormalizePath(basePath)),
        _baseAssetPackage(baseAssetPackage),
        _localAssetNames(),
        _localAssetNamesValid(false)
    {
    }

    BundleAssetPackage::~BundleAssetPackage() {
    }

    std::string BundleAssetPackage::getBasePath() const {
        return _basePath;
    }

    std::vector<std::string> BundleAssetPackage::getLocalAssetNames() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_localAssetNamesValid) {
            _localAssetNames.clear();
            ScanAssets(_basePath, std::string(), _localAssetNames);
            _localAssetNamesValid = true;
        }
        return _localAssetNames;
    }

    void BundleAssetPackage::reload() {
        std::lock_guard<std::mutex> lock(_mutex);

        _localAssetNames.clear();
        _localAssetNamesValid = false;
    }

    std::vector<std::string> BundleAssetPackage::getAssetNames() const {
        std::vector<std::string> names;
        if (_baseAssetPackage) {
            names = _baseAssetPackage->getAssetNames();
        }

        std::vector<std::string> localNames = getLocalAssetNames();
        names.reserve(names.size() + localNames.size());
        for (auto it = localNames.begin(); it != localNames.end(); it++) {
            if (std::find(names.begin(), names.end(), *it) == names.end()) {
                names.push_back(*it);
            }
        }
        return names;
    }

    std::shared_ptr<BinaryData> BundleAssetPackage::loadAsset(const std::string& name) const {
        std::string normalizedName = FileUtils::NormalizePath(name);
        // Do not allow escaping the base directory
        if (normalizedName.empty() || normalizedName.front() == '/' || normalizedName.compare(0, 2, "..") == 0) {
            if (_baseAssetPackage) {
                return _baseAssetPackage->loadAsset(name);
            }
            return std::shared_ptr<BinaryData>();
        }

        std::string path = _basePath.empty() ? normalizedName : _basePath + "/" + normalizedName;
        // AssetExists first: LoadAsset logs an error for a miss, and a miss is the normal answer
        // when the asset only lives in the base package.
        if (AssetUtils::AssetExists(path)) {
            if (std::shared_ptr<BinaryData> data = AssetUtils::LoadAsset(path)) {
                return data;
            }
        }
        if (_baseAssetPackage) {
            return _baseAssetPackage->loadAsset(name);
        }
        return std::shared_ptr<BinaryData>();
    }

    void BundleAssetPackage::ScanAssets(const std::string& basePath, const std::string& subDir, std::vector<std::string>& assetNames) {
        std::string fullPath = basePath;
        if (!subDir.empty()) {
            fullPath = fullPath.empty() ? subDir : fullPath + "/" + subDir;
        }

        // The platform listing cannot tell a file from a directory - an entry is a file if it
        // exists as one, a directory if listing it answers with something.
        for (const std::string& name : AssetUtils::ListAssets(fullPath)) {
            if (name.empty() || name.front() == '.') {
                continue;
            }

            std::string relPath = subDir.empty() ? name : subDir + "/" + name;
            if (AssetUtils::AssetExists(fullPath.empty() ? name : fullPath + "/" + name)) {
                assetNames.push_back(relPath);
            } else {
                ScanAssets(basePath, relPath, assetNames);
            }
        }
    }

}
