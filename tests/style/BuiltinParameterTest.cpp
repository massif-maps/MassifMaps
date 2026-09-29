/*
 * The scales every style has: MBVectorTileDecoder accepts these names without the style declaring
 * them, and SymbolizerContext::Settings reads them. An app sets `_fontscale` on any style.
 */

#include "TestCheck.h"

#include <mapnikvt/SymbolizerContext.h>

namespace mvt = massif::mvt;

void testBuiltinParameters() {
    const auto& builtins = mvt::SymbolizerContext::Settings::getBuiltinParameters();
    TEST_CHECK(builtins.size() == 3, "three built-in scales");
    TEST_CHECK(builtins.at("_fontscale") == 1.0f, "_fontscale defaults to no scaling");
    TEST_CHECK(builtins.at("_geometryscale") == 1.0f, "_geometryscale defaults to no scaling");
    TEST_CHECK(builtins.at("_zoomlevelbias") == 0.0f, "_zoomlevelbias defaults to no bias");
}
