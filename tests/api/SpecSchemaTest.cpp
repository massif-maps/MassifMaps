/*
 * A source class reaches the facade only through its !spec declaration - without one there is no
 * `{"type":…}` factory at all, whatever the SDK class can do. PMTiles had none, so a binding could
 * only build one natively and adopt it.
 *
 * The factory itself is device-verified (see ../README.md), and PMTilesTileDataSource cannot even
 * be linked here - it needs zlib, brotli and zstd. What IS checkable on the host is the schema the
 * generator derives from the .i and the constructors, which is what a binding reads.
 */

#include "core/Variant.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace massif;

#include "TestCheck.h"

namespace {

    Variant readSchema(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return Variant::FromString(buffer.str());
    }

    Variant findSpec(const Variant& schema, const std::string& kind, const std::string& type) {
        Variant specs = schema.getObjectElement("specs");
        for (int i = 0; i < specs.getArraySize(); i++) {
            Variant spec = specs.getArrayElement(i);
            if (spec.getObjectElement("kind").getString() == kind &&
                spec.getObjectElement("type").getString() == type) {
                return spec;
            }
        }
        return Variant();
    }

    // The keys a caller must pass for this overload - a declared default makes one optional.
    int requiredArgs(const Variant& constructor, std::string& lastKey) {
        int count = 0;
        for (int i = 0; i < constructor.getArraySize(); i++) {
            Variant arg = constructor.getArrayElement(i);
            if (arg.getObjectElement("required").getBool()) {
                count++;
                lastKey = arg.getObjectElement("key").getString();
            }
        }
        return count;
    }

}

void testSourceSpecSchema() {
    Variant schema = readSchema(TEST_SCHEMA_PATH);

    Variant pmtiles = findSpec(schema, "source", "pmtiles");
    TEST_CHECK(pmtiles.getObjectElement("cppClass").getString() == "massif::PMTilesTileDataSource",
               "PMTilesTileDataSource declares a `pmtiles` source spec");
    TEST_CHECK(pmtiles.getObjectElement("defaults").getObjectElement("minZoom").getString() == "0" &&
               pmtiles.getObjectElement("defaults").getObjectElement("maxZoom").getString() == "24",
               "the zoom bounds are declared defaults, as on mbtiles");

    // Both overloads are readable, and the path-only spec has to satisfy one of them - that is what
    // makes `{"type":"pmtiles","path":"x"}` a complete spec rather than a missing-argument error.
    Variant constructors = pmtiles.getObjectElement("constructors");
    TEST_CHECK(constructors.getArraySize() == 2, "both PMTiles constructors are in the table");
    bool pathOnly = false;
    for (int i = 0; i < constructors.getArraySize(); i++) {
        std::string key;
        if (requiredArgs(constructors.getArrayElement(i), key) == 1 && key == "path") {
            pathOnly = true;
        }
    }
    TEST_CHECK(pathOnly, "a path alone builds a pmtiles source");

    // The committed schema is what an integration generates its typings from, so a spec that only
    // exists in the .i is still invisible to it.
    Variant committed = readSchema(SDK_SCHEMA_PATH);
    TEST_CHECK(findSpec(committed, "source", "pmtiles").getObjectElement("cppClass").getString() ==
               "massif::PMTilesTileDataSource",
               "docs/api/massif-api.json carries it too (scripts/gen-api-bindings.sh)");
}
