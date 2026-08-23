#include "AssetUtils.h"
#include "core/BinaryData.h"
#include "utils/Log.h"

#include <utf8.h>

#include <stdio.h>
#include <windows.h>

namespace massif {

    namespace {
        /**
         * The installed location joined with a relative asset path, as a wide string.
         *
         * LoadAsset also retries under "Assets\\"; listing does not, because a listing walks down
         * from a root the caller named and a silent second root would make the names ambiguous.
         */
        std::wstring InstalledPath(const std::string& path) {
            std::wstring wpath;
            utf8::utf8to16(path.begin(), path.end(), std::back_inserter(wpath));
            std::wstring root = Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Data();
            return path.empty() ? root : root + L"\\" + wpath;
        }
    }

    std::shared_ptr<BinaryData> AssetUtils::LoadAsset(const std::string& path) {
        std::wstring wpath;
        utf8::utf8to16(path.begin(), path.end(), std::back_inserter(wpath));
        Platform::String^ appPath = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;
        Platform::String^ fullPath = appPath + L"\\" + ref new Platform::String(wpath.c_str());
        std::wstring wfullPath = fullPath->Data();
        FILE* fpRaw = _wfopen(wfullPath.c_str(), L"rb");
        if (!fpRaw) {
            fullPath = appPath + L"\\Assets\\" + ref new Platform::String(wpath.c_str());
            wfullPath = fullPath->Data();
            fpRaw = _wfopen(wfullPath.c_str(), L"rb");
        }
        if (fpRaw) {
            std::shared_ptr<FILE> fp(fpRaw, fclose);
            fseek(fp.get(), 0, SEEK_END);
            long size = ftell(fp.get());
            if (size < 0) {
                Log::Errorf("AssetManager::LoadAsset: Error detecting asset size: %s", path.c_str());
                return std::shared_ptr<BinaryData>();
            }
            std::vector<unsigned char> data(size);
            fseek(fp.get(), 0, SEEK_SET);
            fread(data.data(), 1, size, fp.get());
            return std::make_shared<BinaryData>(std::move(data));
        } else {
            Log::Errorf("AssetUtils::LoadAsset: Asset not found: %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }
    }

    bool AssetUtils::AssetExists(const std::string& path) {
        DWORD attributes = GetFileAttributesW(InstalledPath(path).c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::vector<std::string> AssetUtils::ListAssets(const std::string& path) {
        std::vector<std::string> names;

        std::wstring pattern = InstalledPath(path) + L"\\*";
        WIN32_FIND_DATAW findData;
        HANDLE handle = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &findData,
                                         FindExSearchNameMatch, NULL, 0);
        if (handle == INVALID_HANDLE_VALUE) {
            return names;
        }
        do {
            std::string name;
            const wchar_t* wname = findData.cFileName;
            utf8::utf16to8(wname, wname + wcslen(wname), std::back_inserter(name));
            if (name.empty() || name.front() == '.') {
                continue;
            }
            names.push_back(name);
        } while (FindNextFileW(handle, &findData));
        FindClose(handle);
        return names;
    }

    AssetUtils::AssetUtils() {
    }

}
