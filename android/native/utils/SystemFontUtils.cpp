#include "utils/SystemFontUtils.h"
#include "core/BinaryData.h"
#include "utils/Log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

#include <dirent.h>

#include <vt/FontNames.h>

namespace massif {

    namespace {

        const char* const FONT_DIRECTORIES[] = { "/system/fonts", "/product/fonts", "/system/font", "/data/fonts", nullptr };

        struct FontAlias {
            const char* name;
            const char* fileCandidates; // font files, for the FreeType path of the vector tile labels
            const char* androidFamily;  // Typeface family, for the platform text path of the vector elements
        };

        // Generic/foreign font names mapped to the Android font families, in preference order
        const FontAlias FONT_ALIASES[] = {
            { "arial",          "roboto notosans droidsans",        "sans-serif" },
            { "helvetica",      "roboto notosans droidsans",        "sans-serif" },
            { "helveticaneue",  "roboto notosans droidsans",        "sans-serif" },
            { "verdana",        "roboto notosans droidsans",        "sans-serif" },
            { "tahoma",         "roboto notosans droidsans",        "sans-serif" },
            { "segoeui",        "roboto notosans droidsans",        "sans-serif" },
            { "sansserif",      "roboto notosans droidsans",        "sans-serif" },
            { "sans",           "roboto notosans droidsans",        "sans-serif" },
            { "roboto",         "roboto notosans droidsans",        "sans-serif" },
            { "notosans",       "notosans roboto droidsans",        "sans-serif" },
            { "droidsans",      "droidsans roboto notosans",        "sans-serif" },
            { "timesnewroman",  "notoserif droidserif tinos",       "serif" },
            { "times",          "notoserif droidserif tinos",       "serif" },
            { "georgia",        "notoserif droidserif tinos",       "serif" },
            { "serif",          "notoserif droidserif tinos",       "serif" },
            { "notoserif",      "notoserif droidserif tinos",       "serif" },
            { "droidserif",     "droidserif notoserif tinos",       "serif" },
            { "couriernew",     "droidsansmono notosansmono cousine", "monospace" },
            { "courier",        "droidsansmono notosansmono cousine", "monospace" },
            { "consolas",       "droidsansmono notosansmono cousine", "monospace" },
            { "monospace",      "droidsansmono notosansmono cousine", "monospace" },
            { "mono",           "droidsansmono notosansmono cousine", "monospace" },
            { "cursive",        "dancingscript cutivemono",         "cursive" },
            { nullptr, nullptr, nullptr }
        };

        const char* const STYLE_SUFFIXES[] = { "bolditalic", "boldoblique", "bold", "italic", "oblique", "regular", "book", nullptr };

        // Weights Android exposes as their own family ('sans-serif-light'), unlike the style suffixes
        const char* const FAMILY_WEIGHT_SUFFIXES[] = { "condensed", "medium", "black", "light", "thin", nullptr };

        const char* const DEFAULT_FONTS[] = { "robotoregular", "notosansregular", "droidsans", "robotobold", nullptr };

        std::string normalizeFontName(const std::string& name) {
            std::string normalized;
            for (char c : name) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    normalized.append(1, static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
            }
            return normalized;
        }

        // Maps normalized font names to font files. Built once, the set of installed fonts is fixed.
        const std::map<std::string, std::string>& getSystemFontMap() {
            static std::mutex mutex;
            static std::map<std::string, std::string> fontMap;
            static bool initialized = false;

            std::lock_guard<std::mutex> lock(mutex);
            if (!initialized) {
                initialized = true;
                for (int i = 0; FONT_DIRECTORIES[i]; i++) {
                    DIR* dir = opendir(FONT_DIRECTORIES[i]);
                    if (!dir) {
                        continue;
                    }
                    while (struct dirent* entry = readdir(dir)) {
                        std::string fileName = entry->d_name;
                        std::size_t extPos = fileName.rfind('.');
                        if (extPos == std::string::npos) {
                            continue;
                        }
                        std::string ext = normalizeFontName(fileName.substr(extPos));
                        if (ext != "ttf" && ext != "otf" && ext != "ttc") {
                            continue;
                        }
                        fontMap.emplace(normalizeFontName(fileName.substr(0, extPos)), std::string(FONT_DIRECTORIES[i]) + "/" + fileName);
                    }
                    closedir(dir);
                }
                Log::Infof("SystemFontUtils: found %d system fonts", static_cast<int>(fontMap.size()));
            }
            return fontMap;
        }

        std::string findFontFile(const std::map<std::string, std::string>& fontMap, const std::string& normalizedName) {
            if (normalizedName.empty()) {
                return std::string();
            }

            auto it = fontMap.find(normalizedName);
            if (it != fontMap.end()) {
                return it->second;
            }
            it = fontMap.find(normalizedName + "regular");
            if (it != fontMap.end()) {
                return it->second;
            }

            // Accept a variant of the family ('roboto' -> 'robotomedium'), preferring the shortest name
            for (auto it2 = fontMap.lower_bound(normalizedName); it2 != fontMap.end(); it2++) {
                if (it2->first.compare(0, normalizedName.size(), normalizedName) != 0) {
                    break;
                }
                return it2->second;
            }
            return std::string();
        }

        // Splits a trailing suffix off the normalized name ('arialbold' -> 'arial' + 'bold')
        std::string splitSuffix(const std::string& normalizedName, const char* const* suffixes, std::string& family) {
            family = normalizedName;
            for (int i = 0; suffixes[i]; i++) {
                std::string suffix = suffixes[i];
                if (normalizedName.size() > suffix.size() && normalizedName.compare(normalizedName.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    family = normalizedName.substr(0, normalizedName.size() - suffix.size());
                    return suffix;
                }
            }
            return std::string();
        }

        std::string resolveFontFile(const std::string& name, bool allowFallback) {
            const std::map<std::string, std::string>& fontMap = getSystemFontMap();
            std::string normalizedName = normalizeFontName(name);

            std::string fileName = findFontFile(fontMap, normalizedName);
            if (!fileName.empty()) {
                return fileName;
            }

            std::string family;
            std::string style = splitSuffix(normalizedName, STYLE_SUFFIXES, family);

            // A weight can sit between the family and the style ('Roboto Medium Italic'), and it is
            // not part of any family name here - 'robotomedium' matches no alias, so the whole name
            // went unresolved and the label was drawn in the fallback font. Taking the weight off
            // too leaves the family that does match; the weight itself is applied by the renderer,
            // through the face's variable axes (vt::FontManager).
            std::string weightFamily;
            std::string weight = splitSuffix(family, FAMILY_WEIGHT_SUFFIXES, weightFamily);

            const std::string families[] = { family, weightFamily };
            for (const std::string& candidateFamily : families) {
                if (candidateFamily.empty()) {
                    continue;
                }
                for (int i = 0; FONT_ALIASES[i].name; i++) {
                    if (candidateFamily != FONT_ALIASES[i].name) {
                        continue;
                    }
                    std::string candidates = FONT_ALIASES[i].fileCandidates;
                    for (std::size_t pos = 0; pos < candidates.size(); ) {
                        std::size_t spacePos = candidates.find(' ', pos);
                        std::string candidate = candidates.substr(pos, spacePos == std::string::npos ? std::string::npos : spacePos - pos);
                        pos = (spacePos == std::string::npos ? candidates.size() : spacePos + 1);

                        // Most specific first: the family's own weighted-and-styled file if it ships
                        // one ('sans-serif-medium'), then the styled one, then the family itself.
                        fileName = findFontFile(fontMap, candidate + weight + style);
                        if (fileName.empty()) {
                            fileName = findFontFile(fontMap, candidate + style);
                        }
                        if (fileName.empty()) {
                            fileName = findFontFile(fontMap, candidate);
                        }
                        if (!fileName.empty()) {
                            return fileName;
                        }
                    }
                    break;
                }
            }

            if (!allowFallback) {
                return std::string();
            }

            // Unresolved name, use the default system font
            for (int i = 0; DEFAULT_FONTS[i]; i++) {
                fileName = findFontFile(fontMap, DEFAULT_FONTS[i]);
                if (!fileName.empty()) {
                    return fileName;
                }
            }
            return fontMap.empty() ? std::string() : fontMap.begin()->second;
        }

        // The Typeface family for a name, empty if Android has no family under that name. Preferred
        // over the font file: a family keeps the per-script fallback chain a single file does not.
        std::string resolveFontFamily(const std::string& name) {
            std::string family;
            std::string weight = splitSuffix(normalizeFontName(name), FAMILY_WEIGHT_SUFFIXES, family);
            for (int i = 0; FONT_ALIASES[i].name; i++) {
                if (family != FONT_ALIASES[i].name) {
                    continue;
                }
                std::string androidFamily = FONT_ALIASES[i].androidFamily;
                // Android carries the weight variants of its sans family only
                if (!weight.empty() && androidFamily == "sans-serif") {
                    return androidFamily + "-" + weight;
                }
                return androidFamily;
            }
            return std::string();
        }

    }

    SystemFontUtils::FontMatch SystemFontUtils::MatchFont(const std::string& names) {
        static std::mutex mutex;
        static std::map<std::string, FontMatch> matchCache;

        std::lock_guard<std::mutex> lock(mutex);
        auto it = matchCache.find(names);
        if (it != matchCache.end()) {
            return it->second;
        }

        FontMatch match;
        for (const std::string& name : vt::parseFontNames(names)) {
            match.familyName = resolveFontFamily(name);
            if (!match.familyName.empty()) {
                break;
            }
            match.fileName = resolveFontFile(name, false);
            if (!match.fileName.empty()) {
                break;
            }
        }
        matchCache[names] = match;
        return match;
    }

    std::shared_ptr<BinaryData> SystemFontUtils::LoadFont(const std::string& name, bool allowFallback) {
        std::string fileName = resolveFontFile(name, allowFallback);
        if (fileName.empty()) {
            // Not an error without the fallback: the caller is walking a font list and tries the next name
            if (allowFallback) {
                Log::Errorf("SystemFontUtils::LoadFont: No system font for %s", name.c_str());
            }
            return std::shared_ptr<BinaryData>();
        }

        std::ifstream file(fileName.c_str(), std::ios::binary);
        if (!file) {
            Log::Errorf("SystemFontUtils::LoadFont: Failed to open %s", fileName.c_str());
            return std::shared_ptr<BinaryData>();
        }
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (data.empty()) {
            Log::Errorf("SystemFontUtils::LoadFont: Failed to read %s", fileName.c_str());
            return std::shared_ptr<BinaryData>();
        }
        Log::Infof("SystemFontUtils::LoadFont: Using %s for %s", fileName.c_str(), name.c_str());
        return std::make_shared<BinaryData>(std::move(data));
    }

    SystemFontUtils::SystemFontUtils() {
    }

}
