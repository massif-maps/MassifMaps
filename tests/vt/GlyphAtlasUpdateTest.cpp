// GlyphMap::getAtlasUpdate: what a texture that last synced at some version still lacks. The texture
// is grown row by row, so a wrong dirty range is a glyph that never reaches the GPU, or a re-upload
// of the whole atlas on every new glyph. Not covered: the GL upload itself - a device check.

#include "GlyphMap.h"

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    std::shared_ptr<const Bitmap> solid(int width, int height, std::uint32_t texel) {
        return std::make_shared<Bitmap>(width, height, std::vector<std::uint32_t>(static_cast<std::size_t>(width) * height, texel));
    }
}

void testGlyphAtlasUpdate() {
    GlyphMap map(64, 64);
    GlyphMap::AtlasUpdate update;

    TEST_CHECK(map.getAtlasUpdate(0, 0, 0, update) && update.full && update.width == 1 && update.height == 1, "a texture never synced gets the whole (empty) atlas");
    std::uint64_t version = update.version;
    TEST_CHECK(!map.getAtlasUpdate(version, update.width, update.height, update), "and nothing more while no glyph is loaded");

    GlyphMap::GlyphId first = map.loadBitmapGlyph(solid(40, 6, 0xAABBCCDD), GlyphMap::GlyphMode::SDF);
    TEST_CHECK(first != 0, "a glyph loads");
    TEST_CHECK(map.getAtlasUpdate(version, 1, 1, update) && update.full && update.width == 64 && update.height == 8, "growing past the synced power-of-two size sends the whole atlas");
    TEST_CHECK(update.texels.size() == 64u * 8u, "the full update covers every row");
    const GlyphMap::Glyph* glyph = map.getGlyph(first);
    TEST_CHECK(glyph && update.texels[glyph->y * update.width + glyph->x] == 0xAABBCCDD, "the glyph's texel sits where the glyph says it does");
    version = update.version;
    int width = update.width;
    int height = update.height;

    TEST_CHECK(!map.getAtlasUpdate(version, width, height, update), "a synced texture asks for nothing");

    // It lands beside the first glyph, inside the 64 x 8 the texture has, so only its rows are due.
    GlyphMap::GlyphId second = map.loadBitmapGlyph(solid(3, 2, 0x11223344), GlyphMap::GlyphMode::SDF);
    TEST_CHECK(second != 0, "a second glyph loads");
    TEST_CHECK(map.getAtlasUpdate(version, width, height, update), "a glyph loaded after the sync is due");
    TEST_CHECK(!update.full, "inside the synced size it is a partial update");
    const GlyphMap::Glyph* next = map.getGlyph(second);
    TEST_CHECK(next && update.y0 <= next->y && update.y1 >= next->y + next->height, "the dirty rows cover the new glyph");
    TEST_CHECK(update.texels.size() == static_cast<std::size_t>(update.width) * (update.y1 - update.y0), "and carry exactly those rows");
    TEST_CHECK(next && update.texels[(next->y - update.y0) * update.width + next->x] == 0x11223344, "the new texel is in the update");
    TEST_CHECK(update.y1 - update.y0 < height, "a row-neighbour of the first glyph is not re-sent whole");
    version = update.version;

    // A sync that skipped a version still gets everything it missed.
    GlyphMap::GlyphId third = map.loadBitmapGlyph(solid(3, 2, 0x55667788), GlyphMap::GlyphMode::SDF);
    GlyphMap::GlyphId fourth = map.loadBitmapGlyph(solid(3, 2, 0x99AABBCC), GlyphMap::GlyphMode::SDF);
    TEST_CHECK(third != 0 && fourth != 0, "two more glyphs load");
    TEST_CHECK(map.getAtlasUpdate(version, width, height, update) && !update.full, "two glyphs behind is still partial");
    const GlyphMap::Glyph* g3 = map.getGlyph(third);
    const GlyphMap::Glyph* g4 = map.getGlyph(fourth);
    TEST_CHECK(g3 && g4 && update.y0 <= std::min(g3->y, g4->y) && update.y1 >= std::max(g3->y + g3->height, g4->y + g4->height), "the range spans both");
    TEST_CHECK(g4 && update.texels[(g4->y - update.y0) * update.width + g4->x] == 0x99AABBCC, "and holds the later glyph's texel");

    // A glyph that repeats loads nothing and so changes nothing.
    std::uint64_t synced = update.version;
    std::shared_ptr<const Bitmap> repeated = solid(4, 4, 0x01020304);
    map.loadBitmapGlyph(repeated, GlyphMap::GlyphMode::SDF);
    map.getAtlasUpdate(synced, width, height, update);
    synced = update.version;
    map.loadBitmapGlyph(repeated, GlyphMap::GlyphMode::SDF);
    TEST_CHECK(!map.getAtlasUpdate(synced, update.width, update.height, update), "a bitmap already in the atlas leaves the version alone");
}
