#include "DirAssetPackage.h"
#include "core/BinaryData.h"
#include "components/Exceptions.h"
#include "utils/FileUtils.h"
#include "utils/Log.h"

#include <algorithm>

#include <stdext/utf8_filesystem.h>

#ifdef _WIN32
#include <utf8.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace massif {

    DirAssetPackage::DirAssetPackage(const std::string& dirPath) :
        _dirPath(FileUtils::NormalizePath(dirPath)),
        _baseAssetPackage(),
        _localAssetNames(),
        _localAssetNamesValid(false)
    {
        if (!IsDirectory(_dirPath)) {
            throw FileException("Could not open asset directory", dirPath);
        }
    }

    DirAssetPackage::DirAssetPackage(const std::string& dirPath, const std::shared_ptr<AssetPackage>& baseAssetPackage) :
        _dirPath(FileUtils::NormalizePath(dirPath)),
        _baseAssetPackage(baseAssetPackage),
        _localAssetNames(),
        _localAssetNamesValid(false)
    {
        if (!IsDirectory(_dirPath)) {
            throw FileException("Could not open asset directory", dirPath);
        }
    }

    DirAssetPackage::~DirAssetPackage() {
    }

    std::string DirAssetPackage::getDirPath() const {
        return _dirPath;
    }

    std::vector<std::string> DirAssetPackage::getLocalAssetNames() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_localAssetNamesValid) {
            _localAssetNames.clear();
            ScanDir(_dirPath, std::string(), _localAssetNames);
            _localAssetNamesValid = true;
        }
        return _localAssetNames;
    }

    void DirAssetPackage::reload() {
        std::lock_guard<std::mutex> lock(_mutex);

        _localAssetNames.clear();
        _localAssetNamesValid = false;
    }

    std::vector<std::string> DirAssetPackage::getAssetNames() const {
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

    std::shared_ptr<BinaryData> DirAssetPackage::loadAsset(const std::string& name) const {
        std::string normalizedName = FileUtils::NormalizePath(name);
        // Do not allow escaping the asset directory
        if (normalizedName.empty() || normalizedName.front() == '/' || normalizedName.compare(0, 2, "..") == 0) {
            if (_baseAssetPackage) {
                return _baseAssetPackage->loadAsset(name);
            }
            return std::shared_ptr<BinaryData>();
        }

        std::string fileName = _dirPath + "/" + normalizedName;
        // Check before wrapping: shared_ptr runs the deleter even on a null pointer, and
        // fclose(nullptr) aborts the process under FORTIFY
        FILE* fpRaw = utf8_filesystem::fopen(fileName.c_str(), "rb");
        if (!fpRaw) {
            if (_baseAssetPackage) {
                return _baseAssetPackage->loadAsset(name);
            }
            return std::shared_ptr<BinaryData>();
        }
        std::shared_ptr<FILE> fp(fpRaw, fclose);

        std::vector<unsigned char> data;
        if (fseek(fp.get(), 0, SEEK_END) == 0) {
            long size = ftell(fp.get());
            if (size > 0) {
                data.reserve(static_cast<std::size_t>(size));
            }
            fseek(fp.get(), 0, SEEK_SET);
        }

        unsigned char buf[4096];
        while (std::size_t count = fread(buf, 1, sizeof(buf), fp.get())) {
            data.insert(data.end(), buf, buf + count);
        }
        if (ferror(fp.get())) {
            Log::Errorf("DirAssetPackage::loadAsset: Could not read asset %s", name.c_str());
            return std::shared_ptr<BinaryData>();
        }
        return std::make_shared<BinaryData>(data.data(), data.size());
    }

    bool DirAssetPackage::IsDirectory(const std::string& path) {
        utf8_filesystem::stat st;
        if (utf8_filesystem::fstat(path.c_str(), &st) != 0) {
            return false;
        }
        return (st.st_mode & S_IFMT) == S_IFDIR;
    }

    void DirAssetPackage::ScanDir(const std::string& dirPath, const std::string& subDir, std::vector<std::string>& assetNames) {
        std::string fullDirPath = dirPath + (subDir.empty() ? "" : "/" + subDir);

#ifdef _WIN32
        std::wstring wpattern;
        std::string pattern = fullDirPath + "/*";
        utf8::utf8to16(pattern.begin(), pattern.end(), std::back_inserter(wpattern));

        WIN32_FIND_DATAW findData;
        HANDLE handle = FindFirstFileExW(wpattern.c_str(), FindExInfoBasic, &findData, FindExSearchNameMatch, NULL, 0);
        if (handle == INVALID_HANDLE_VALUE) {
            Log::Errorf("DirAssetPackage::ScanDir: Could not read directory %s", fullDirPath.c_str());
            return;
        }
        do {
            std::string name;
            const wchar_t* wname = findData.cFileName;
            utf8::utf16to8(wname, wname + wcslen(wname), std::back_inserter(name));
            if (name.empty() || name.front() == '.') {
                continue;
            }

            std::string relPath = subDir.empty() ? name : subDir + "/" + name;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ScanDir(dirPath, relPath, assetNames);
            } else {
                assetNames.push_back(relPath);
            }
        } while (FindNextFileW(handle, &findData));
        FindClose(handle);
#else
        std::shared_ptr<DIR> dir(opendir(fullDirPath.c_str()), [](DIR* dir) { if (dir) closedir(dir); });
        if (!dir) {
            Log::Errorf("DirAssetPackage::ScanDir: Could not read directory %s", fullDirPath.c_str());
            return;
        }
        while (struct dirent* entry = readdir(dir.get())) {
            std::string name = entry->d_name;
            if (name.empty() || name.front() == '.') {
                continue;
            }

            std::string relPath = subDir.empty() ? name : subDir + "/" + name;
            // d_type is not available on all file systems, fall back to stat
            bool isDir = entry->d_type == DT_DIR;
            if (entry->d_type == DT_UNKNOWN) {
                isDir = IsDirectory(fullDirPath + "/" + name);
            }
            if (isDir) {
                ScanDir(dirPath, relPath, assetNames);
            } else {
                assetNames.push_back(relPath);
            }
        }
#endif
    }

}
