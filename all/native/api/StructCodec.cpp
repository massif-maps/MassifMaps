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

    namespace {
        /** "#aarrggbb" - what a style writes and what an app reads back. */
        std::string encodeColor(const Color& color) {
            static const char* HEX = "0123456789abcdef";
            unsigned int argb = static_cast<unsigned int>(color.getARGB());
            std::string out = "#";
            for (int shift = 28; shift >= 0; shift -= 4) {
                out += HEX[(argb >> shift) & 0xf];
            }
            return out;
        }

        /** Lenient: "#rgb", "#rrggbb", "#aarrggbb", or the plain ARGB number the facade uses. */
        bool decodeColor(const Variant& value, Color& color) {
            if (value.getType() == VariantType::VARIANT_TYPE_INTEGER || value.getType() == VariantType::VARIANT_TYPE_DOUBLE) {
                color = Color(static_cast<unsigned int>(value.getLong()));
                return true;
            }
            if (value.getType() != VariantType::VARIANT_TYPE_STRING) {
                return false;
            }
            std::string text = value.getString();
            if (text.empty() || text[0] != '#') {
                return false;
            }
            std::string digits = text.substr(1);
            if (digits.size() == 3) {
                std::string expanded;
                for (char ch : digits) {
                    expanded += ch;
                    expanded += ch;
                }
                digits = expanded;
            }
            if (digits.size() == 6) {
                digits = "ff" + digits;
            }
            if (digits.size() != 8) {
                return false;
            }
            unsigned int argb = 0;
            for (char ch : digits) {
                int digit = ch >= '0' && ch <= '9' ? ch - '0'
                          : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                          : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
                if (digit < 0) {
                    return false;
                }
                argb = (argb << 4) | static_cast<unsigned int>(digit);
            }
            color = Color(argb);
            return true;
        }
    }

    std::string encode(const LightStop& value) {
        std::ostringstream stream;
        stream.precision(17);
        stream << "{\"sunAltitude\":" << value.getSunAltitude()
               << ",\"ambientColor\":\"" << encodeColor(value.getAmbientColor()) << "\""
               << ",\"ambientIntensity\":" << value.getAmbientIntensity()
               << ",\"sunColor\":\"" << encodeColor(value.getSunColor()) << "\""
               << ",\"sunIntensity\":" << value.getSunIntensity() << "}";
        return stream.str();
    }

    std::string encode(const std::vector<LightStop>& value) {
        std::string json = "[";
        for (std::size_t index = 0; index < value.size(); index++) {
            json += (index ? "," : "") + encode(value[index]);
        }
        return json + "]";
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

    std::string encode(const std::vector<std::vector<MapPos> >& value) {
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

    bool decode(const std::string& json, LightStop& value) {
        std::map<std::string, Variant> fields;
        if (!decode(json, fields)) {
            return false;
        }
        Color ambientColor(255, 255, 255, 255), sunColor(255, 255, 255, 255);
        // Every field is optional but a stop with no sun height is meaningless, so that one is not.
        if (!fields.count("sunAltitude")) {
            return false;
        }
        if (fields.count("ambientColor") && !decodeColor(fields["ambientColor"], ambientColor)) {
            return false;
        }
        if (fields.count("sunColor") && !decodeColor(fields["sunColor"], sunColor)) {
            return false;
        }
        value = LightStop(static_cast<float>(fields["sunAltitude"].getDouble()),
                          ambientColor, static_cast<float>(fields["ambientIntensity"].getDouble()),
                          sunColor, static_cast<float>(fields["sunIntensity"].getDouble()));
        return true;
    }

    bool decode(const std::string& json, std::vector<LightStop>& value) {
        // Clearing the curve is how an app goes back to the built-in one, and a property set to ""
        // is what that spells through every binding. Refusing it left the previous curve standing,
        // which reads as "the switch is broken" rather than as a rejected value.
        if (json.empty()) {
            value.clear();
            return true;
        }
        Variant array;
        if (!decode(json, array) || array.getType() != VariantType::VARIANT_TYPE_ARRAY) {
            return false;
        }
        std::vector<LightStop> stops;
        for (int index = 0; index < array.getArraySize(); index++) {
            LightStop stop;
            if (!decode(array.getArrayElement(index).toString(), stop)) {
                return false;
            }
            stops.push_back(stop);
        }
        value.swap(stops);
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

    bool decode(const std::string& json, std::vector<std::vector<MapPos> >& value) {
        Variant array;
        if (!decode(json, array) || array.getType() != VariantType::VARIANT_TYPE_ARRAY) {
            return false;
        }
        std::vector<std::vector<MapPos> > items;
        for (int index = 0; index < array.getArraySize(); index++) {
            std::vector<MapPos> ring;
            if (!decode(array.getArrayElement(index).toString(), ring)) {
                return false;
            }
            items.push_back(ring);
        }
        value.swap(items);
        return true;
    }

    bool readEntry(const std::string& entry, PropertyValue& value) {
        value = PropertyValue::ofString(entry);
        return true;
    }

    bool readEntry(const Variant& entry, PropertyValue& value) {
        switch (entry.getType()) {
        case VariantType::VARIANT_TYPE_NULL:    return false;
        case VariantType::VARIANT_TYPE_BOOL:    value = PropertyValue::ofBool(entry.getBool()); break;
        case VariantType::VARIANT_TYPE_INTEGER: value = PropertyValue::ofLong(entry.getLong()); break;
        case VariantType::VARIANT_TYPE_DOUBLE:  value = PropertyValue::ofDouble(entry.getDouble()); break;
        case VariantType::VARIANT_TYPE_STRING:  value = PropertyValue::ofString(entry.getString()); break;
        default:
            // An object or an array reads as its JSON, the same as a Variant property does.
            value = PropertyValue::ofString(entry.toString());
            value.type = PT_VARIANT;
            break;
        }
        return true;
    }

    void writeEntry(const PropertyValue& value, std::string& entry) {
        entry = value.asString();
    }

    void writeEntry(const PropertyValue& value, Variant& entry) {
        switch (value.type) {
        case PT_BOOL:  entry = Variant(value.asBool()); break;
        case PT_INT:
        case PT_COLOR:
        case PT_ENUM:  entry = Variant(value.asLong()); break;
        case PT_FLOAT: entry = Variant(value.asDouble()); break;
        case PT_VARIANT:
            // Already JSON. A string that does not parse is kept as the string it is.
            try {
                entry = Variant::FromString(value.stringValue);
            } catch (const std::exception&) {
                entry = Variant(value.stringValue);
            }
            break;
        default:       entry = Variant(value.asString()); break;
        }
    }

} } }
