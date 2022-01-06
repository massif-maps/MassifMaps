#include "StringUtils.h"

#include <cstdint>
#include <utility>
#include <algorithm>
#include <regex>

#include <stdext/unistring.h>

namespace carto::mvt {
    std::size_t stringLength(const std::string& str) {
        unistring::unistring unistr = unistring::to_unistring(str);
        return unistr.size();
    }

    std::string toUpper(const std::string& str) {
        unistring::unistring unistr = unistring::to_unistring(str);
        return unistring::to_utf8string(unistring::to_upper(unistr));
    }

    std::string toLower(const std::string& str) {
        unistring::unistring unistr = unistring::to_unistring(str);
        return unistring::to_utf8string(unistring::to_lower(unistr));
    }

    std::string capitalize(const std::string& str) {
        unistring::unistring unistr = unistring::to_unistring(str);
        return unistring::to_utf8string(unistring::capitalize(unistr));
    }

    std::string stringReverse(const std::string& str) {
        unistring::unistring unistr = unistring::to_unistring(str);
        std::reverse(unistr.begin(), unistr.end());
        return unistring::to_utf8string(unistr);
    }

    bool regexMatch(const std::string& str, const std::string& re) {
        unistring::unistring unistr = unistring::to_unistring(str);
        unistring::unistring unire = unistring::to_unistring(re);
        return std::regex_match(unistring::to_wstring(unistr), std::wregex(unistring::to_wstring(unire)));
    }

    std::string regexReplace(const std::string& str, const std::string& re, const std::string& replacement) {
        unistring::unistring unistr = unistring::to_unistring(str);
        unistring::unistring unire = unistring::to_unistring(re);
        unistring::unistring unireplacement = unistring::to_unistring(replacement);
        unistring::unistring uniresult = unistring::to_unistring(std::regex_replace(unistring::to_wstring(unistr), std::wregex(unistring::to_wstring(unire)), unistring::to_wstring(unireplacement)));
        return unistring::to_utf8string(uniresult);
    }
}
