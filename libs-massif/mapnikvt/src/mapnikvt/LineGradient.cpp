#include "LineGradient.h"
#include "ParserUtils.h"
#include "ValueConverter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace massif::mvt {
    namespace {
        struct Stop {
            double position;
            vt::Color color;
        };

        std::shared_ptr<const vt::ColorRamp> internRamp(std::vector<std::uint8_t> texels) {
            static std::mutex mutex;
            static std::unordered_multimap<std::size_t, std::weak_ptr<const vt::ColorRamp>> ramps;

            std::size_t hash = std::hash<std::string_view>()(std::string_view(reinterpret_cast<const char*>(texels.data()), texels.size()));
            std::lock_guard<std::mutex> lock(mutex);
            auto range = ramps.equal_range(hash);
            for (auto it = range.first; it != range.second; ) {
                if (std::shared_ptr<const vt::ColorRamp> ramp = it->second.lock()) {
                    if (ramp->texels == texels) {
                        return ramp;
                    }
                    ++it;
                }
                else {
                    it = ramps.erase(it);
                }
            }
            auto ramp = std::make_shared<vt::ColorRamp>();
            ramp->texels = std::move(texels);
            ramps.emplace(hash, ramp);
            return ramp;
        }

        bool parseColorValue(const Value& value, vt::Color& color) {
            if (const long long* packed = std::get_if<long long>(&value)) {
                color = vt::Color::fromValue(static_cast<unsigned int>(*packed));
                return true;
            }
            const std::string* text = std::get_if<std::string>(&value);
            return text && tryParseColor(*text, color);
        }

        bool parseStopsText(std::string text, std::vector<Stop>& stops, bool& step) {
            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
            };
            trim(text);
            if (text.compare(0, 5, "step;") == 0) {
                step = true;
                text.erase(0, 5);
            }

            int depth = 0;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= text.size(); i++) {
                if (i < text.size()) {
                    depth += (text[i] == '(') - (text[i] == ')');
                    if (text[i] != ',' || depth != 0) {
                        continue;
                    }
                }
                std::string pair = text.substr(start, i - start);
                start = i + 1;
                trim(pair);
                std::size_t colon = pair.find(':');
                vt::Color color;
                if (colon == std::string::npos || !tryParseColor(pair.substr(colon + 1), color)) {
                    return false;
                }
                std::string position = pair.substr(0, colon);
                char* end = nullptr;
                double value = std::strtod(position.c_str(), &end);
                if (position.empty() || *end != '\0' || !std::isfinite(value)) {
                    return false;
                }
                stops.push_back({ value, premultiplyColor(color) });
            }
            return !stops.empty();
        }

        bool parseStopsArray(const ValueArray& array, std::vector<Stop>& stops) {
            for (const Value& element : array.elements) {
                const auto* pair = std::get_if<std::shared_ptr<const ValueArray>>(&element);
                vt::Color color;
                if (!pair || !*pair || (*pair)->elements.size() < 2 || !parseColorValue((*pair)->elements[1], color)) {
                    return false;
                }
                double position = ValueConverter<double>::convert((*pair)->elements[0]);
                if (!std::isfinite(position)) {
                    return false;
                }
                stops.push_back({ position, premultiplyColor(color) });
            }
            return !stops.empty();
        }
    }

    std::shared_ptr<const vt::ColorRamp> buildLineGradient(const std::function<vt::Color(double)>& colorAt) {
        std::vector<std::uint8_t> texels(static_cast<std::size_t>(vt::ColorRamp::WIDTH) * 4);
        for (int i = 0; i < vt::ColorRamp::WIDTH; i++) {
            std::array<std::uint8_t, 4> rgba = colorAt((i + 0.5) / vt::ColorRamp::WIDTH).rgba8();
            std::copy(rgba.begin(), rgba.end(), texels.begin() + i * 4);
        }
        return internRamp(std::move(texels));
    }

    std::shared_ptr<const vt::ColorRamp> buildLineGradient(const Value& value) {
        std::vector<Stop> stops;
        bool step = false;
        if (const std::string* text = std::get_if<std::string>(&value)) {
            std::size_t first = text->find_first_not_of(" \t\r\n");
            if (first != std::string::npos && (*text)[first] == '[') {
                Value parsed = valueFromJSON(*text);
                const auto* array = std::get_if<std::shared_ptr<const ValueArray>>(&parsed);
                if (!array || !*array || !parseStopsArray(**array, stops)) {
                    return nullptr;
                }
            }
            else if (!parseStopsText(*text, stops, step)) {
                return nullptr;
            }
        }
        else if (const auto* array = std::get_if<std::shared_ptr<const ValueArray>>(&value)) {
            if (!*array || !parseStopsArray(**array, stops)) {
                return nullptr;
            }
        }
        else {
            return nullptr;
        }

        std::stable_sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) { return a.position < b.position; });
        return buildLineGradient([&stops, step](double t) {
            // The last stop at or before t holds, so of two stops at one position the second wins past it.
            auto next = std::upper_bound(stops.begin(), stops.end(), t, [](double t, const Stop& stop) { return t < stop.position; });
            if (next == stops.begin()) {
                return stops.front().color;
            }
            const Stop& previous = *(next - 1);
            if (step || next == stops.end()) {
                return previous.color;
            }
            float weight = static_cast<float>((t - previous.position) / (next->position - previous.position));
            vt::Color result;
            for (int c = 0; c < 4; c++) {
                result[c] = previous.color[c] + (next->color[c] - previous.color[c]) * weight;
            }
            return result;
        });
    }
}
