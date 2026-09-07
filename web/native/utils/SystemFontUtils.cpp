#include "utils/SystemFontUtils.h"
#include "core/BinaryData.h"
#include "utils/Log.h"

#include <cstdio>

#include <sys/stat.h>

#include <vt/FontNames.h>

namespace massif {

    namespace {
        // A browser has no font API a renderer can read glyph outlines out of, so "the system
        // fonts" here are whatever the host page put in the virtual filesystem. Styles that ship
        // their own fonts (all of ours do) never reach this.
        const char* const FONT_ROOT = "/fonts";

        const char* const FONT_EXTENSIONS[] = { ".ttf", ".otf", ".ttc", nullptr };

        std::string findFontFile(const std::string& name) {
            for (int i = 0; FONT_EXTENSIONS[i]; i++) {
                std::string path = std::string(FONT_ROOT) + "/" + name + FONT_EXTENSIONS[i];
                struct stat info;
                if (::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
                    return path;
                }
            }
            return std::string();
        }
    }

    SystemFontUtils::FontMatch SystemFontUtils::MatchFont(const std::string& names) {
        FontMatch match;
        for (const std::string& name : vt::parseFontNames(names)) {
            std::string fileName = findFontFile(name);
            if (!fileName.empty()) {
                match.familyName = name;
                match.fileName = fileName;
                break;
            }
        }
        return match;
    }

    std::shared_ptr<BinaryData> SystemFontUtils::LoadFont(const std::string& name, bool allowFallback) {
        std::string path = findFontFile(name);
        if (path.empty()) {
            // Not an error without the fallback: the caller is walking a font list and tries the next name
            if (allowFallback) {
                Log::Errorf("SystemFontUtils::LoadFont: No font file for %s", name.c_str());
            }
            return std::shared_ptr<BinaryData>();
        }

        std::shared_ptr<std::FILE> file(std::fopen(path.c_str(), "rb"), [](std::FILE* fp) {
            if (fp) {
                std::fclose(fp);
            }
        });
        if (!file || std::fseek(file.get(), 0, SEEK_END) != 0) {
            Log::Errorf("SystemFontUtils::LoadFont: Failed to read %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }
        long size = std::ftell(file.get());
        if (size < 0) {
            Log::Errorf("SystemFontUtils::LoadFont: Failed to read %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }
        std::rewind(file.get());

        std::vector<unsigned char> data(static_cast<std::size_t>(size));
        if (std::fread(data.data(), 1, data.size(), file.get()) != data.size()) {
            Log::Errorf("SystemFontUtils::LoadFont: Failed to read %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }
        return std::make_shared<BinaryData>(std::move(data));
    }

    SystemFontUtils::SystemFontUtils() {
    }

}
