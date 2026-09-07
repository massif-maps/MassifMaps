#include "utils/AssetUtils.h"
#include "core/BinaryData.h"
#include "utils/Log.h"

#include <cstdio>
#include <mutex>

#include <dirent.h>
#include <sys/stat.h>

namespace massif {

    namespace {
        std::mutex _RootMutex;
        std::string _AssetRoot = "/assets";
    }

    void AssetUtils::SetAssetRoot(const std::string& root) {
        std::lock_guard<std::mutex> lock(_RootMutex);
        _AssetRoot = root;
    }

    std::string AssetUtils::GetFullPath(const std::string& path) {
        std::lock_guard<std::mutex> lock(_RootMutex);
        if (path.empty()) {
            return _AssetRoot;
        }
        if (path.front() == '/') {
            return _AssetRoot + path;
        }
        return _AssetRoot + "/" + path;
    }

    std::shared_ptr<BinaryData> AssetUtils::LoadAsset(const std::string& path) {
        std::string fullPath = GetFullPath(path);
        std::shared_ptr<std::FILE> file(std::fopen(fullPath.c_str(), "rb"), [](std::FILE* fp) {
            if (fp) {
                std::fclose(fp);
            }
        });
        if (!file) {
            Log::Errorf("AssetUtils::LoadAsset: Asset not found: %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }

        if (std::fseek(file.get(), 0, SEEK_END) != 0) {
            Log::Errorf("AssetUtils::LoadAsset: Asset is not seekable: %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }
        long size = std::ftell(file.get());
        if (size < 0) {
            Log::Errorf("AssetUtils::LoadAsset: Asset size is <0: %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }
        std::rewind(file.get());

        std::vector<unsigned char> data(static_cast<std::size_t>(size));
        if (std::fread(data.data(), 1, data.size(), file.get()) != data.size()) {
            Log::Errorf("AssetUtils::LoadAsset: Asset read failed: %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }
        return std::make_shared<BinaryData>(std::move(data));
    }

    bool AssetUtils::AssetExists(const std::string& path) {
        struct stat info;
        if (::stat(GetFullPath(path).c_str(), &info) != 0) {
            return false;
        }
        return S_ISREG(info.st_mode);
    }

    std::vector<std::string> AssetUtils::ListAssets(const std::string& path) {
        std::vector<std::string> names;
        std::shared_ptr<DIR> dir(::opendir(GetFullPath(path).c_str()), [](DIR* d) {
            if (d) {
                ::closedir(d);
            }
        });
        if (!dir) {
            return names;
        }
        while (const struct dirent* entry = ::readdir(dir.get())) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            names.push_back(name);
        }
        return names;
    }

    AssetUtils::AssetUtils() {
    }

}
