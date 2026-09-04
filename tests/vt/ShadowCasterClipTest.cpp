/*
 * The extrusion caster keeps the drawn extrusion's tile clip.
 *
 * polygon3DFsh discards every fragment outside the half-open target tile, so under overzoom each
 * target tile draws its own piece of the source geometry and a buffer-margin copy from the
 * neighbouring source tile never reaches the screen. The caster used to draw all of it: a copy
 * whose base was still unresolved stood above the drawn building and shadowed its whole roof.
 * Both shaders must keep the SAME clip, or the shadow map and the screen disagree again.
 */

#include <map>
#include <string>

#include "GLTileRendererShaders.h" // declares nothing it includes: <map> and <string> first
#include "TestCheck.h"

namespace {
    std::string clipClause(const std::string& shader) {
        std::string::size_type start = shader.find("if (vTilePos.x < 0.0");
        if (start == std::string::npos) {
            return std::string();
        }
        std::string::size_type end = shader.find("discard;", start);
        return end == std::string::npos ? std::string() : shader.substr(start, end - start);
    }
}

void testShadowCasterClip() {
    using namespace massif::vt;

    std::string drawn = clipClause(polygon3DFsh);
    std::string caster = clipClause(polygon3DShadowCasterFsh);
    TEST_CHECK(!drawn.empty(), "the drawn extrusion clips to its target tile");
    TEST_CHECK(!caster.empty(), "so does the extrusion caster");
    TEST_CHECK(drawn == caster, "with the same half-open bounds, so the map matches the screen");
    TEST_CHECK(clipClause(shadowCasterFsh).empty(),
               "the terrain caster has no tile clip to keep: it draws exactly the drawn surfaces");
}
