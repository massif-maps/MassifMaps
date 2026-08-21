#include "api/Methods.h"
#include "api/StructCodec.h"
#include "core/MapPos.h"
#include "core/MapTile.h"

#include <map>
#include <mutex>
#include <string>

namespace massif { namespace api {

    CallArgs::CallArgs() {
    }

    bool CallArgs::parse(const std::string& json, CallArgs& args) {
        if (json.empty()) {
            args._array = Variant(std::vector<Variant>());
            return true;
        }
        Variant array;
        try {
            array = Variant::FromString(json);
        } catch (const std::exception&) {
            return false;
        }
        if (array.getType() != VariantType::VARIANT_TYPE_ARRAY) {
            return false;
        }
        args._array = array;
        return true;
    }

    int CallArgs::count() const {
        return _array.getArraySize();
    }

    Variant CallArgs::get(int index) const {
        return index >= 0 && index < _array.getArraySize() ? _array.getArrayElement(index) : Variant();
    }

    bool CallArgs::getBool(int index, bool& value) const {
        Variant argument = get(index);
        if (argument.getType() != VariantType::VARIANT_TYPE_BOOL) {
            return false;
        }
        value = argument.getBool();
        return true;
    }

    bool CallArgs::getLong(int index, long long& value) const {
        Variant argument = get(index);
        if (argument.getType() != VariantType::VARIANT_TYPE_INTEGER) {
            return false;
        }
        value = argument.getLong();
        return true;
    }

    bool CallArgs::getDouble(int index, double& value) const {
        Variant argument = get(index);
        // An integer is a valid double: JSON has one number type, and 3 is not a different
        // argument from 3.0.
        if (argument.getType() == VariantType::VARIANT_TYPE_INTEGER) {
            value = static_cast<double>(argument.getLong());
            return true;
        }
        if (argument.getType() != VariantType::VARIANT_TYPE_DOUBLE) {
            return false;
        }
        value = argument.getDouble();
        return true;
    }

    bool CallArgs::getString(int index, std::string& value) const {
        Variant argument = get(index);
        if (argument.getType() != VariantType::VARIANT_TYPE_STRING) {
            return false;
        }
        value = argument.getString();
        return true;
    }

    bool CallArgs::getHandle(int index, Handle& value) const {
        long long number = 0;
        if (!getLong(index, number) || number < 0 || number > 0xffffffffLL) {
            return false;
        }
        value = static_cast<Handle>(number);
        return true;
    }

    bool CallArgs::getPos(int index, MapPos& value) const {
        Variant argument = get(index);
        return argument.getType() == VariantType::VARIANT_TYPE_ARRAY &&
               StructCodec::decode(argument.toString(), value);
    }

    bool CallArgs::getPositions(int index, std::vector<MapPos>& value) const {
        Variant argument = get(index);
        return argument.getType() == VariantType::VARIANT_TYPE_ARRAY &&
               StructCodec::decode(argument.toString(), value);
    }

    bool CallArgs::getTile(int index, MapTile& value) const {
        Variant argument = get(index);
        if (argument.getType() != VariantType::VARIANT_TYPE_ARRAY || argument.getArraySize() != 3) {
            return false;
        }
        long long parts[3];
        for (int element = 0; element < 3; element++) {
            if (argument.getArrayElement(element).getType() != VariantType::VARIANT_TYPE_INTEGER) {
                return false;
            }
            parts[element] = argument.getArrayElement(element).getLong();
        }
        // frameNr 0: a tile addressed from outside the map belongs to no frame.
        value = MapTile(static_cast<int>(parts[0]), static_cast<int>(parts[1]),
                        static_cast<int>(parts[2]), 0);
        return true;
    }

    namespace Methods {

        namespace {
            std::mutex& mutex() {
                static std::mutex instance;
                return instance;
            }

            std::map<std::string, std::map<std::string, MethodInvoke> >& registry() {
                static std::map<std::string, std::map<std::string, MethodInvoke> > instance;
                return instance;
            }
        }

        void registerMethod(const char* cppClass, const char* name, MethodInvoke invoke) {
            std::lock_guard<std::mutex> lock(mutex());
            registry()[cppClass][name] = invoke;
        }

        MethodInvoke findMethod(const char* cppClass, const std::string& name) {
            std::lock_guard<std::mutex> lock(mutex());
            // The base chain comes from the generated table, so a method declared on a base is
            // callable on every subclass without being registered again.
            for (const ClassEntry* entry = findClass(cppClass); entry;
                 entry = entry->base ? findClass(entry->base) : nullptr) {
                auto classIt = registry().find(entry->cppClass);
                if (classIt == registry().end()) {
                    continue;
                }
                auto methodIt = classIt->second.find(name);
                if (methodIt != classIt->second.end()) {
                    return methodIt->second;
                }
            }
            return nullptr;
        }

    }

} }
