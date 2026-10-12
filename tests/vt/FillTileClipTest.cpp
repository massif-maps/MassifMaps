/*
 * A fill on the terrain clips to its target tile, as a line does.
 *
 * Terrain draws no tile stencil masks, and a fill triangle is kept whole once it touches the tile,
 * so an overzoomed landuse triangle reaches hundreds of metres past it. Drawn live (a tile no drape
 * leaf covers), it stood above the drape wherever the ground dipped under its chord and blanked a
 * park's grass and paths there. Source text only: whether the clip holds is a device check.
 */

#include <map>
#include <string>

#include "GLTileRendererShaders.h" // declares nothing it includes: <map> and <string> first
#include "TestCheck.h"

namespace {
    std::string clipClause(const std::string& shader) {
        std::string::size_type start = shader.find("if (vTileUnit.x < -0.0005");
        if (start == std::string::npos) {
            return std::string();
        }
        std::string::size_type end = shader.find("discard;", start);
        return end == std::string::npos ? std::string() : shader.substr(start, end - start);
    }

    // The preprocessor line guarding the first occurrence of `token`.
    std::string guardOf(const std::string& shader, const std::string& token) {
        std::string::size_type at = shader.find(token);
        if (at == std::string::npos) {
            return std::string();
        }
        std::string::size_type guard = shader.rfind("#if", at);
        if (guard == std::string::npos || shader.rfind("#endif", at) > guard) {
            return std::string();
        }
        return shader.substr(guard, shader.find('\n', guard) - guard);
    }
}

void testFillTileClip() {
    using namespace massif::vt;

    std::string line = clipClause(lineFsh);
    std::string fill = clipClause(polygonFsh);
    TEST_CHECK(!line.empty(), "a terrain line discards outside its target tile");
    TEST_CHECK(!fill.empty(), "so does a terrain fill");
    TEST_CHECK(fill == line, "with the same bounds, so a fill and its outline stop at the same edge");
    TEST_CHECK(guardOf(polygonFsh, "if (vTileUnit.x < -0.0005") == "#ifdef TERRAIN",
               "the fill clip runs on the terrain, not only under a drape mask");
    TEST_CHECK(guardOf(polygonVsh, "vTileUnit = pos.xy").find("TERRAIN") != std::string::npos,
               "the fill vertex stage writes the tile position on the terrain too");
}
