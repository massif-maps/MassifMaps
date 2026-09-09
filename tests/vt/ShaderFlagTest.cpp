/*
 * Every shader flag owns a bit, and every flag reaches flagDefineMap.
 *
 * flagDefineMap is a std::map keyed by the flag VALUE, so two flags sharing a bit silently drop
 * the second one's #define - the code behind it is dead, and asking for it compiles the other
 * one's instead. DRAPE_MASK sat on SPAN's bit that way and never once ran.
 */

#include <map>
#include <string>
#include <vector>

#include "GLTileRendererShaders.h" // declares nothing it includes: <map> and <string> first
#include "TestCheck.h"

void testShaderFlags() {
    using namespace massif::vt;

    const std::vector<std::pair<unsigned int, std::string>> flags = {
        { TRANSFORM_FLAG, "TRANSFORM" },
        { OFFSET_FLAG, "OFFSET" },
        { PATTERN_FLAG, "PATTERN" },
        { DERIVATIVES_FLAG, "DERIVATIVES" },
        { TERRAIN_FLAG, "TERRAIN_DEPTH_BIAS" },
        { TERRAIN_VTF_FLAG, "TERRAIN" },
        { DRAPE_FLAG, "DRAPE" },
        { TERRAIN_LIGHT_FLAG, "TERRAIN_LIGHT" },
        { TERRAIN_SHADOW_FLAG, "TERRAIN_SHADOW" },
        { PAINT_SURFACE_FLAG, "PAINT_SURFACE" },
        { FOG_FLAG, "FOG" },
        { GROUND_BASE_FLAG, "GROUND_BASE" },
        { DEM_HW_FILTER_FLAG, "DEM_HW_FILTER" },
        { SHADOW_CASCADES2_FLAG, "SHADOW_CASCADES_2" },
        { SHADOW_CASCADES3_FLAG, "SHADOW_CASCADES_3" },
        { SHADOW_CASCADES4_FLAG, "SHADOW_CASCADES_4" },
        { SHADOW_MASK_OUT_FLAG, "SHADOW_MASK_OUT" },
        { SHADOW_MASK_IN_FLAG, "SHADOW_MASK_IN" },
        { SHADOW_SINGLE_TAP_FLAG, "SHADOW_SINGLE_TAP" },
        { SHADOW_DEPTH_TEXTURE_FLAG, "SHADOW_DEPTH_TEXTURE" },
        { ESSL3_FLAG, "ESSL3" },
        { SHADOW_HW_FLAG, "SHADOW_HW" },
        { GEOMETRY_LIGHT_FLAG, "GEOMETRY_LIGHT" },
        { LABEL_OCCLUSION_FLAG, "LABEL_OCCLUSION" },
        { COVERAGE_FLAG, "COVERAGE" },
        { SPAN_FLAG, "SPAN" },
        { DRAPE_MASK_FLAG, "DRAPE_MASK" },
        { GAPWIDTH_FLAG, "GAPWIDTH" },
        { BLUR_FLAG, "BLUR" },
        { SPAN_DRAPE_FLAG, "SPAN_DRAPE" },
        { SHADOW_RECEIVER_3D_FLAG, "SHADOW_RECEIVER_3D" }
    };

    TEST_CHECK(flags.size() == flagDefineMap.size(),
               "a new flag is listed here too - the count is what catches a collided one");

    for (const std::pair<unsigned int, std::string>& flag : flags) {
        std::map<unsigned int, std::string>::const_iterator it = flagDefineMap.find(flag.first);
        TEST_CHECK(it != flagDefineMap.end() && it->second == flag.second,
                   ("flag " + flag.second + " defines its own name").c_str());
        // A single bit each: the renderer ORs them and the map looks each one up on its own.
        TEST_CHECK(flag.first != 0 && (flag.first & (flag.first - 1)) == 0,
                   ("flag " + flag.second + " is one bit").c_str());
    }
}
