/*
 * A FontManager with no FreeType behind it: every name resolves to one monospace face whose glyphs
 * are boxes, so a scenario lays labels out by their text alone. Linked in place of vt/FontManager.cpp.
 */

#include "FontManager.h"

namespace massif::vt {
    namespace {
        class BoxFont final : public Font {
        public:
            explicit BoxFont(const std::string& name) : _name(name), _glyphMap(std::make_shared<GlyphMap>(64, 64)) { }

            Metrics getMetrics(float size) const override { return Metrics(0.9f * size, -0.25f * size, 1.15f * size); }

            std::vector<Glyph> shapeGlyphs(const std::uint32_t* utf32Text, std::size_t len, float size, bool) const override {
                std::vector<Glyph> glyphs;
                for (std::size_t i = 0; i < len; i++) {
                    bool space = (utf32Text[i] == ' ');
                    GlyphMap::Glyph base(GlyphMap::GlyphMode::SDF, 0, 0, space ? 0 : 1, space ? 0 : 1, cglib::vec2<float>(0, 0));
                    glyphs.emplace_back(utf32Text[i], utf32Text[i], base, space ? cglib::vec2<float>(0, 0) : cglib::vec2<float>(0.52f, 0.75f) * size,
                                        cglib::vec2<float>(0.04f, -0.75f) * size, cglib::vec2<float>(space ? 0.3f : 0.6f, 0) * size);
                }
                return glyphs;
            }

            std::shared_ptr<GlyphMap> getGlyphMap() const override { return _glyphMap; }
            int getGlyphRenderSize() const override { return GLYPH_RENDER_SIZE; }
            const std::string& getName() const override { return _name; }

        private:
            std::string _name;
            std::shared_ptr<GlyphMap> _glyphMap;
        };
    }

    class FontManager::Impl { };

    FontManager::FontManager(int, int) : _impl(std::make_unique<Impl>()) { }
    FontManager::~FontManager() = default;

    std::string FontManager::loadFontData(const std::vector<unsigned char>&) { return "Scenario"; }
    void FontManager::addPendingFontData(const std::string&, FontDataProvider) { }
    void FontManager::setFontDataLoader(FontDataLoader) { }

    std::shared_ptr<const Font> FontManager::getFont(const std::string& name, const std::shared_ptr<const Font>&) const {
        return std::make_shared<BoxFont>(name);
    }

    std::shared_ptr<const Font> FontManager::getFont(const std::shared_ptr<const Font>& font, int) const {
        return font;
    }
}
