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

    std::string encode(const Variant& value) {
        return value.toString();
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

} } }
