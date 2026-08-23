#include "api/StructCodec.h"

#include <sstream>
#include <vector>

namespace massif { namespace api { namespace StructCodec {

    namespace {

        std::string encodeNumbers(const std::vector<double>& numbers) {
            std::ostringstream stream;
            stream.precision(17);
            stream << "[";
            for (std::size_t index = 0; index < numbers.size(); index++) {
                stream << (index ? "," : "") << numbers[index];
            }
            stream << "]";
            return stream.str();
        }

        /**
         * Reads a JSON array of numbers. Fewer than min is a failure; a missing trailing element
         * up to max is left at whatever the caller pre-filled, which is how z defaults to 0.
         */
        bool decodeNumbers(const std::string& json, std::size_t min, std::size_t max,
                           std::vector<double>& out) {
            Variant variant;
            try {
                variant = Variant::FromString(json);
            } catch (const std::exception&) {
                return false;
            }
            if (variant.getType() != VariantType::VARIANT_TYPE_ARRAY) {
                return false;
            }
            std::size_t count = static_cast<std::size_t>(variant.getArraySize());
            if (count < min || count > max) {
                return false;
            }
            for (std::size_t index = 0; index < count; index++) {
                Variant element = variant.getArrayElement(static_cast<int>(index));
                if (element.getType() == VariantType::VARIANT_TYPE_INTEGER) {
                    out[index] = static_cast<double>(element.getLong());
                } else if (element.getType() == VariantType::VARIANT_TYPE_DOUBLE) {
                    out[index] = element.getDouble();
                } else {
                    return false;
                }
            }
            return true;
        }

    }

    std::string encode(const MapPos& value) {
        return encodeNumbers({ value.getX(), value.getY(), value.getZ() });
    }

    std::string encode(const MapVec& value) {
        return encodeNumbers({ value.getX(), value.getY(), value.getZ() });
    }

    std::string encode(const ScreenPos& value) {
        return encodeNumbers({ value.getX(), value.getY() });
    }

    std::string encode(const MapRange& value) {
        return encodeNumbers({ value.getMin(), value.getMax() });
    }

    std::string encode(const MapBounds& value) {
        return "[" + encode(value.getMin()) + "," + encode(value.getMax()) + "]";
    }

    std::string encode(const ScreenBounds& value) {
        return "[" + encode(value.getMin()) + "," + encode(value.getMax()) + "]";
    }

    std::string encode(const Variant& value) {
        return value.toString();
    }

    std::string encode(const std::vector<std::string>& value) {
        std::vector<Variant> items;
        for (const std::string& item : value) {
            items.push_back(Variant(item));
        }
        return Variant(items).toString();
    }

    // frameNr is not part of the spelling: a tile addressed from outside the map belongs to no
    // frame, which is the same reason CallArgs::getTile writes 0.
    std::string encode(const MapTile& value) {
        return "[" + std::to_string(value.getX()) + "," + std::to_string(value.getY()) + "," +
               std::to_string(value.getZoom()) + "]";
    }

    std::string encode(const ClickInfo& value) {
        std::map<std::string, Variant> fields;
        fields["clickType"] = Variant(static_cast<long long>(value.getClickType()));
        fields["duration"] = Variant(static_cast<double>(value.getDuration()));
        return Variant(fields).toString();
    }

    std::string encode(const std::map<std::string, std::string>& value) {
        std::map<std::string, Variant> variants;
        for (const std::pair<const std::string, std::string>& item : value) {
            variants[item.first] = Variant(item.second);
        }
        return encode(variants);
    }

    std::string encode(const std::map<std::string, Variant>& value) {
        return Variant(value).toString();
    }

    std::string encode(const std::vector<MapPos>& value) {
        std::string json = "[";
        for (std::size_t index = 0; index < value.size(); index++) {
            json += (index ? "," : "") + encode(value[index]);
        }
        return json + "]";
    }

    bool decode(const std::string& json, MapPos& value) {
        std::vector<double> numbers(3, 0);
        if (!decodeNumbers(json, 2, 3, numbers)) {
            return false;
        }
        value = MapPos(numbers[0], numbers[1], numbers[2]);
        return true;
    }

    bool decode(const std::string& json, MapVec& value) {
        std::vector<double> numbers(3, 0);
        if (!decodeNumbers(json, 2, 3, numbers)) {
            return false;
        }
        value = MapVec(numbers[0], numbers[1], numbers[2]);
        return true;
    }

    bool decode(const std::string& json, ScreenPos& value) {
        std::vector<double> numbers(2, 0);
        if (!decodeNumbers(json, 2, 2, numbers)) {
            return false;
        }
        value = ScreenPos(static_cast<float>(numbers[0]), static_cast<float>(numbers[1]));
        return true;
    }

    bool decode(const std::string& json, MapRange& value) {
        std::vector<double> numbers(2, 0);
        if (!decodeNumbers(json, 2, 2, numbers)) {
            return false;
        }
        value = MapRange(static_cast<float>(numbers[0]), static_cast<float>(numbers[1]));
        return true;
    }

    bool decode(const std::string& json, MapBounds& value) {
        Variant variant;
        try {
            variant = Variant::FromString(json);
        } catch (const std::exception&) {
            return false;
        }
        if (variant.getType() != VariantType::VARIANT_TYPE_ARRAY || variant.getArraySize() != 2) {
            return false;
        }
        MapPos min, max;
        if (!decode(variant.getArrayElement(0).toString(), min) ||
            !decode(variant.getArrayElement(1).toString(), max)) {
            return false;
        }
        value = MapBounds(min, max);
        return true;
    }

    bool decode(const std::string& json, Variant& value) {
        try {
            value = Variant::FromString(json);
        } catch (const std::exception&) {
            return false;
        }
        return true;
    }

    bool decode(const std::string& json, std::vector<std::string>& value) {
        Variant array;
        if (!decode(json, array) || array.getType() != VariantType::VARIANT_TYPE_ARRAY) {
            return false;
        }
        std::vector<std::string> items;
        for (int index = 0; index < array.getArraySize(); index++) {
            Variant item = array.getArrayElement(index);
            if (item.getType() != VariantType::VARIANT_TYPE_STRING) {
                return false;   // a list of names, not of anything
            }
            items.push_back(item.getString());
        }
        value.swap(items);
        return true;
    }

    bool decode(const std::string& json, ScreenBounds& value) {
        Variant variant;
        try {
            variant = Variant::FromString(json);
        } catch (const std::exception&) {
            return false;
        }
        if (variant.getType() != VariantType::VARIANT_TYPE_ARRAY || variant.getArraySize() != 2) {
            return false;
        }
        ScreenPos min, max;
        if (!decode(variant.getArrayElement(0).toString(), min) ||
            !decode(variant.getArrayElement(1).toString(), max)) {
            return false;
        }
        value = ScreenBounds(min, max);
        return true;
    }

    bool decode(const std::string& json, MapTile& value) {
        std::vector<double> numbers(3, 0);
        if (!decodeNumbers(json, 3, 3, numbers)) {
            return false;
        }
        value = MapTile(static_cast<int>(numbers[0]), static_cast<int>(numbers[1]),
                        static_cast<int>(numbers[2]), 0);
        return true;
    }

    bool decode(const std::string& json, ClickInfo& value) {
        std::map<std::string, Variant> fields;
        if (!decode(json, fields) || !fields.count("clickType")) {
            return false;
        }
        value = ClickInfo(static_cast<ClickType::ClickType>(fields["clickType"].getLong()),
                          static_cast<float>(fields["duration"].getDouble()));
        return true;
    }

    bool decode(const std::string& json, std::map<std::string, Variant>& value) {
        Variant object;
        if (!decode(json, object) || object.getType() != VariantType::VARIANT_TYPE_OBJECT) {
            return false;
        }
        std::map<std::string, Variant> items;
        for (const std::string& key : object.getObjectKeys()) {
            items[key] = object.getObjectElement(key);
        }
        value.swap(items);
        return true;
    }

    bool decode(const std::string& json, std::map<std::string, std::string>& value) {
        std::map<std::string, Variant> variants;
        if (!decode(json, variants)) {
            return false;
        }
        std::map<std::string, std::string> items;
        for (const std::pair<const std::string, Variant>& item : variants) {
            // A header value is text; a number written as one is spelled out rather than refused.
            items[item.first] = item.second.getType() == VariantType::VARIANT_TYPE_STRING
                              ? item.second.getString() : item.second.toString();
        }
        value.swap(items);
        return true;
    }

    bool decode(const std::string& json, std::vector<MapPos>& value) {
        Variant array;
        if (!decode(json, array) || array.getType() != VariantType::VARIANT_TYPE_ARRAY) {
            return false;
        }
        std::vector<MapPos> items;
        for (int index = 0; index < array.getArraySize(); index++) {
            MapPos pos;
            if (!decode(array.getArrayElement(index).toString(), pos)) {
                return false;
            }
            items.push_back(pos);
        }
        value.swap(items);
        return true;
    }

} } }
