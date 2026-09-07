#include "FontNames.h"

#include <cctype>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace massif::vt {
    namespace {
        const char* const PLATFORM_TAGS[] = { "android", "ios", "macos", "windows", nullptr };

        const char* currentPlatformTag() {
#if defined(__ANDROID__)
            return "android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
            return "ios";
#elif defined(__APPLE__)
            return "macos";
#elif defined(_WIN32)
            return "windows";
#else
            return "";
#endif
        }

        std::string trimFontName(const std::string& name) {
            std::size_t begin = 0;
            std::size_t end = name.size();
            while (begin < end && std::isspace(static_cast<unsigned char>(name[begin]))) {
                begin++;
            }
            while (end > begin && std::isspace(static_cast<unsigned char>(name[end - 1]))) {
                end--;
            }
            if (end - begin >= 2 && (name[begin] == '"' || name[begin] == '\'') && name[end - 1] == name[begin]) {
                begin++;
                end--;
            }
            return name.substr(begin, end - begin);
        }

        // The tag of "android:Roboto", or an empty string when the entry does not carry one
        std::string extractPlatformTag(const std::string& name) {
            std::size_t colonPos = name.find(':');
            if (colonPos == std::string::npos) {
                return std::string();
            }
            std::string tag;
            for (std::size_t i = 0; i < colonPos; i++) {
                tag.append(1, static_cast<char>(std::tolower(static_cast<unsigned char>(name[i]))));
            }
            for (int i = 0; PLATFORM_TAGS[i]; i++) {
                if (tag == PLATFORM_TAGS[i]) {
                    return tag;
                }
            }
            return std::string();
        }
    }

    std::vector<std::string> parseFontNames(const std::string& names) {
        std::vector<std::string> fontNames;
        for (std::size_t pos = 0; pos <= names.size(); ) {
            std::size_t commaPos = names.find(',', pos);
            std::string name = trimFontName(names.substr(pos, commaPos == std::string::npos ? std::string::npos : commaPos - pos));
            pos = (commaPos == std::string::npos ? names.size() + 1 : commaPos + 1);

            std::string tag = extractPlatformTag(name);
            if (!tag.empty()) {
                if (tag != currentPlatformTag()) {
                    continue;
                }
                name = trimFontName(name.substr(tag.size() + 1));
            }
            if (!name.empty()) {
                fontNames.push_back(name);
            }
        }
        return fontNames;
    }
}
