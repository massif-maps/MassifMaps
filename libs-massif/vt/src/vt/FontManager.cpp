#include "FontManager.h"
#include "Font.h"
#include "GlyphMap.h"

#include <atomic>
#include <cctype>
#include <mutex>
#include <memory>
#include <array>
#include <map>
#include <set>
#include <unordered_map>

#include <freetype/freetype.h>
#include <freetype/ftsnames.h>
#include <freetype/ttnameid.h>
#include <freetype/ftrender.h>
#include <freetype/ftstroke.h>
#include <freetype/ftmodapi.h>

#include <hb.h>
#include <hb-ft.h>

namespace massif::vt {
    class FontManagerLibrary {
    public:
        FontManagerLibrary() : _library(nullptr) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            FT_Init_FreeType(&_library);

            // The glyph field is built from a rasterized coverage bitmap by the 'bsdf' module,
            // never from the outline by the 'sdf' one - see addFreeTypeGlyph for why. Both carry
            // their own spread, and both have to match the range the field is encoded over.
            FT_Int spread = GLYPH_RENDER_SPREAD;
            FT_Property_Set(_library, "sdf", "spread", &spread);
            FT_Property_Set(_library, "bsdf", "spread", &spread);
        }

        ~FontManagerLibrary() {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            FT_Done_FreeType(_library);
            _library = nullptr;
        }

        FT_Library getLibrary() const {
            return _library;
        }

        std::recursive_mutex& getMutex() const {
            return _mutex;
        }

    private:
        FT_Library _library;
        static std::recursive_mutex _mutex; // use global lock as harfbuzz is not thread-safe on every platform
    };

    std::recursive_mutex FontManagerLibrary::_mutex;

    class FontManagerFont : public Font {
    public:
        explicit FontManagerFont(const std::shared_ptr<FontManagerLibrary>& library, const std::string& name, const std::shared_ptr<GlyphMap>& glyphMap, const std::vector<unsigned char>* data, const std::shared_ptr<const Font>& baseFont, int glyphRenderSize) : _library(library), _name(name), _baseFont(baseFont), _glyphMap(glyphMap), _glyphRenderSize(glyphRenderSize), _face(nullptr), _font(nullptr) {
            std::lock_guard<std::recursive_mutex> lock(_library->getMutex());

            // Load FreeType font
            if (data) {
                int error = FT_New_Memory_Face(_library->getLibrary(), data->data(), static_cast<FT_Long>(data->size()), 0, &_face);
                if (error == 0) {
                    int renderSize = _glyphRenderSize - GLYPH_RENDER_SPREAD;
                    error = FT_Set_Char_Size(_face, 0, static_cast<int>(renderSize * 64.0f), 0, 0);
                }
            }

            // Create HarfBuzz font
            if (_face) {
                _font = hb_ft_font_create(_face, nullptr);
                if (_font) {
                    hb_ft_font_set_funcs(_font);
                }
            }
            
            // Initialize HarfBuzz buffer for glyph shaping
            _buffer = hb_buffer_create();
            if (_buffer) {
                hb_buffer_set_unicode_funcs(_buffer, hb_unicode_funcs_get_default());
            }
        }

        virtual ~FontManagerFont() {
            std::lock_guard<std::recursive_mutex> lock(_library->getMutex());
            
            if (_buffer) {
                hb_buffer_destroy(_buffer);
                _buffer = nullptr;
            }

            if (_font) {
                hb_font_destroy(_font);
                _font = nullptr;
            }

            if (_face) {
                FT_Done_Face(_face);
                _face = nullptr;
            }
        }

        virtual Metrics getMetrics(float size) const override {
            int renderSize = _glyphRenderSize - GLYPH_RENDER_SPREAD;
            float ascent = _face->size->metrics.ascender / 64.0f * size / renderSize;
            float descent = _face->size->metrics.descender / 64.0f * size / renderSize;
            float height = _face->size->metrics.height / 64.0f * size / renderSize;
            return Metrics(ascent, descent, height);
        }

        virtual std::vector<Glyph> shapeGlyphs(const std::uint32_t* utf32Text, std::size_t len, float size, bool rtl) const override {
            std::lock_guard<std::recursive_mutex> lock(_library->getMutex());

            // Find first font that covers all the characters. If not possible, use the last
            unsigned int fontId = 0;
            const FontManagerFont* font = nullptr;
            for (const FontManagerFont* currentFont = this; currentFont; fontId++) {
                if (currentFont->_font) {
                    font = currentFont;
                    hb_buffer_clear_contents(_buffer);
                    hb_buffer_add_utf32(_buffer, utf32Text, static_cast<unsigned int>(len), 0, static_cast<unsigned int>(len));
                    hb_buffer_set_direction(_buffer, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                    hb_buffer_guess_segment_properties(_buffer);
                    hb_shape(font->_font, _buffer, nullptr, 0);

                    unsigned int infoCount = 0;
                    const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(_buffer, &infoCount);
                    bool allValid = std::all_of(info, info + infoCount, [](const hb_glyph_info_t& glyphInfo) { return glyphInfo.codepoint != 0; });
                    if (allValid) {
                        break;
                    }
                }

                currentFont = dynamic_cast<const FontManagerFont*>(currentFont->_baseFont.get());
            }
            if (!font) {
                return std::vector<Glyph>();
            }

            // Get glyph list and glyph positions
            unsigned int infoCount = 0;
            const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(_buffer, &infoCount);
            unsigned int posCount = 0;
            const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(_buffer, &posCount);

            // Copy glyphs, render/cache bitmaps
            std::vector<Glyph> glyphs;
            glyphs.reserve(infoCount);
            for (unsigned int i = 0; i < infoCount; i++) {
                if (info[i].codepoint != 0) { // ignore 'missing glyph' glyphs
                    CodePoint remappedCodePoint = info[i].codepoint | (fontId << 24);
                    auto it = _codePointGlyphMap.find(remappedCodePoint);
                    if (it == _codePointGlyphMap.end()) {
                        GlyphMap::GlyphId glyphId = addFreeTypeGlyph(font->_face, info[i].codepoint);
                        if (!glyphId) {
                            continue;
                        }
                        it = _codePointGlyphMap.insert({ remappedCodePoint, glyphId }).first;
                    }
                    if (const GlyphMap::Glyph* baseGlyph = _glyphMap->getGlyph(it->second)) {
                        std::size_t cluster = info[i].cluster;
                        std::uint32_t utf32Char = (cluster < len ? utf32Text[cluster] : 0);
                        // The glyph was rasterized and shaped by 'font', which is this font or one
                        // of its fallbacks - and a fallback can carry a different render size, so
                        // the metrics have to be scaled by the size they actually came out at.
                        int renderSize = font->_glyphRenderSize - GLYPH_RENDER_SPREAD;
                        float glyphScale = size / renderSize;
                        cglib::vec2<float> glyphSize(static_cast<float>(baseGlyph->width), static_cast<float>(baseGlyph->height));
                        Glyph glyph(utf32Char, info[i].codepoint, *baseGlyph, glyphSize * glyphScale, baseGlyph->origin * glyphScale, cglib::vec2<float>(0, 0));
                        glyphs.push_back(glyph);
                        if (i < posCount) {
                            glyphs.back().offset += cglib::vec2<float>(pos[i].x_offset / 64.0f, pos[i].y_offset / 64.0f) * glyphScale;
                            glyphs.back().advance = cglib::vec2<float>(pos[i].x_advance / 64.0f, pos[i].y_advance / 64.0f) * glyphScale;
                        }
                    }
                }
            }
            return glyphs;
        }

        virtual std::shared_ptr<GlyphMap> getGlyphMap() const override {
            return _glyphMap;
        }

        virtual int getGlyphRenderSize() const override {
            return _glyphRenderSize;
        }

        virtual const std::string& getName() const override {
            return _name;
        }

        const std::shared_ptr<const Font>& getBaseFont() const {
            return _baseFont;
        }

    private:
        static constexpr int RENDER_SIZE = GLYPH_RENDER_SIZE - GLYPH_RENDER_SPREAD;

        GlyphMap::GlyphId addFreeTypeGlyph(FT_Face face, CodePoint codePoint) const {
            FT_Error error = FT_Load_Glyph(face, codePoint, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
            if (error != 0) {
                return 0;
            }
            // Rasterize coverage first, then let FreeType's 'bsdf' module build the field from
            // that bitmap - the same thing tangram does (fontContext.cpp rasterizes, then
            // sdfBuildDistanceFieldNoAlloc). FreeType's outline SDF ('sdf' module, which is what
            // rendering straight to FT_RENDER_MODE_SDF uses) gets the sign wrong where a stem
            // meets a shoulder, so the middle of a stroke reads as outside the glyph.
            error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
            if (error != 0 && error != FT_Err_Cannot_Render_Glyph) {
                return 0;
            }
            error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF);
            if (error != 0 && error != FT_Err_Cannot_Render_Glyph) {
                return 0;
            }

            int width  = face->glyph->bitmap.width;
            int height = face->glyph->bitmap.rows;
            float xOffset = std::ceil(-face->glyph->metrics.horiBearingX / 64.0f);
            float yOffset = std::ceil((face->glyph->metrics.height - face->glyph->metrics.horiBearingY) / 64.0f);
            // FreeType writes +-GLYPH_RENDER_SPREAD texels over +-127; the renderer's convention is
            // 128 / BITMAP_SDF_SCALE per texel (BitmapCanvas). Converting between the two is this
            // ratio - and it has to be exactly this, or the field stops short of 0 at the edge of
            // the bitmap and every halo turns into a box (see GLYPH_RENDER_SPREAD).
            float distScale = (128.0f / BITMAP_SDF_SCALE) * (GLYPH_RENDER_SPREAD / 127.0f);
            const unsigned char* distBuffer = face->glyph->bitmap.buffer;
            if (!distBuffer) {
                width = height = 0;
            }

            std::vector<std::uint32_t> glyphBitmapData(width * height);
            for (std::size_t i = 0; i < glyphBitmapData.size(); i++) {
                float dist = (distBuffer[i] - 128.0f) * distScale;
                std::uint32_t val = static_cast<std::uint32_t>(std::max(0.0f, std::min(255.0f, dist + 128.0f)));
                glyphBitmapData[i] = val * ((1U << 24) | (1U << 16) | (1U << 8) | 1U);
            }
            std::shared_ptr<Bitmap> glyphBitmap = std::make_shared<Bitmap>(width, height, std::move(glyphBitmapData));
            return _glyphMap->loadBitmapGlyph(glyphBitmap, GlyphMap::GlyphMode::SDF, cglib::vec2<float>(-xOffset, -GLYPH_RENDER_SPREAD - yOffset));
        }

        const std::shared_ptr<FontManagerLibrary> _library;
        const std::string _name;
        const std::shared_ptr<const Font> _baseFont;
        std::shared_ptr<GlyphMap> _glyphMap;
        const int _glyphRenderSize;
        mutable std::unordered_map<CodePoint, GlyphMap::GlyphId> _codePointGlyphMap;
        FT_Face _face;
        hb_font_t* _font;
        hb_buffer_t* _buffer;
    };

    class FontManager::Impl {
    public:
        explicit Impl(int maxGlyphMapWidth, int maxGlyphMapHeight) : _maxGlyphMapWidth(maxGlyphMapWidth), _maxGlyphMapHeight(maxGlyphMapHeight), _library(std::make_shared<FontManagerLibrary>()) { }

        std::string loadFontData(const std::vector<unsigned char>& data) {
            std::lock_guard<std::mutex> lock(_mutex);

            return loadFontDataUnlocked(data);
        }

        void addPendingFontData(const std::string& hintName, FontDataProvider dataProvider) {
            std::lock_guard<std::mutex> lock(_mutex);

            if (dataProvider) {
                _pendingFonts.push_back({ normalizeFontName(hintName), std::move(dataProvider) });
            }
        }

        // Note: _mutex is expected to be locked by the caller
        std::string loadFontDataUnlocked(const std::vector<unsigned char>& data) const {
            if (data.empty()) {
                return std::string();
            }

            FontManagerLibrary library;
            FT_Face face;
            int error = FT_New_Memory_Face(library.getLibrary(), data.data(), static_cast<FT_Long>(data.size()), 0, &face);
            if (error != 0) {
                return std::string();
            }
            std::string fullName, family, subFamily;
            for (unsigned int i = 0; i < FT_Get_Sfnt_Name_Count(face); i++) {
                FT_SfntName sfntName;
                error = FT_Get_Sfnt_Name(face, i, &sfntName);
                if (error != 0) {
                    continue;
                }
                std::string name = readSfntName(sfntName);
                if (name.empty()) {
                    continue;
                }
                switch (sfntName.name_id) {
                case TT_NAME_ID_FULL_NAME:
                    fullName = name;
                    break;
                case TT_NAME_ID_FONT_FAMILY:
                case TT_NAME_ID_PREFERRED_FAMILY:
                    family = name;
                    break;
                case TT_NAME_ID_FONT_SUBFAMILY:
                case TT_NAME_ID_PREFERRED_SUBFAMILY:
                    subFamily = name;
                    break;
                default:
                    break;
                }
            }

            std::string registeredName = fullName;
            if (!fullName.empty()) {
                _fontDataMap[fullName] = data;
            }
            if (!family.empty()) {
                if (!subFamily.empty()) {
                    _fontDataMap[family + " " + subFamily] = data;
                    if (registeredName.empty()) {
                        registeredName = family + " " + subFamily;
                    }
                }
                else {
                    _fontDataMap[family] = data;
                    if (registeredName.empty()) {
                        registeredName = family;
                    }
                }
            }
            FT_Done_Face(face);
            return registeredName;
        }

        void setFontDataLoader(FontDataLoader loader) {
            std::lock_guard<std::mutex> lock(_mutex);

            _fontDataLoader = std::move(loader);
        }

        std::shared_ptr<const Font> getFont(const std::string& name, const std::shared_ptr<const Font>& baseFont) const {
            std::lock_guard<std::mutex> lock(_mutex);

            return getFontUnlocked(name, baseFont);
        }

        std::shared_ptr<const Font> getFont(const std::shared_ptr<const Font>& font, int glyphRenderSize) const {
            std::lock_guard<std::mutex> lock(_mutex);

            return getFontAtSizeUnlocked(font, glyphRenderSize);
        }

    private:
        // Note: _mutex is expected to be locked by the caller
        std::shared_ptr<const Font> getFontAtSizeUnlocked(const std::shared_ptr<const Font>& font, int glyphRenderSize) const {
            auto managerFont = std::dynamic_pointer_cast<const FontManagerFont>(font);
            if (!managerFont || managerFont->getGlyphRenderSize() == glyphRenderSize) {
                return font;
            }
            const std::string& name = managerFont->getName();
            if (name.find("glyph_size=") != std::string::npos) {
                return font; // the style asked for a render size by name - it wins
            }
            // The fallbacks come along: a glyph taken from one of them is rasterized by that font,
            // at that font's render size (see shapeGlyphs).
            std::shared_ptr<const Font> baseFont = getFontAtSizeUnlocked(managerFont->getBaseFont(), glyphRenderSize);
            std::string sizedName = name + (name.find('?') != std::string::npos ? "&" : "?") + "glyph_size=" + std::to_string(glyphRenderSize);
            if (std::shared_ptr<const Font> sizedFont = getFontUnlocked(sizedName, baseFont)) {
                return sizedFont;
            }
            return font;
        }

        // Note: _mutex is expected to be locked by the caller
        std::shared_ptr<const Font> getFontUnlocked(const std::string& name, const std::shared_ptr<const Font>& baseFont) const {
            // Try to use already cached font
            auto fontIt = _fontMap.find(std::make_pair(name, baseFont));
            if (fontIt != _fontMap.end()) {
                return fontIt->second;
            }

            // Parse font name and query parameters
            std::string fontName = name;
            int glyphRenderSize = GLYPH_RENDER_SIZE;
            
            size_t queryPos = name.find('?');
            if (queryPos != std::string::npos) {
                fontName = name.substr(0, queryPos);
                std::string queryString = name.substr(queryPos + 1);
                
                // Parse query parameters (e.g., "glyph_size=64")
                size_t pos = 0;
                while (pos < queryString.length()) {
                    size_t eqPos = queryString.find('=', pos);
                    if (eqPos == std::string::npos) break;
                    
                    std::string key = queryString.substr(pos, eqPos - pos);
                    size_t nextAmpPos = queryString.find('&', eqPos + 1);
                    std::string value = queryString.substr(eqPos + 1, nextAmpPos == std::string::npos ? std::string::npos : nextAmpPos - eqPos - 1);
                    
                    if (key == "glyph_size") {
                        try {
                            int parsedSize = std::stoi(value);
                            // Validate: must be between 8 and 512 pixels
                            if (parsedSize >= 8 && parsedSize <= 512) {
                                glyphRenderSize = parsedSize;
                            }
                        } catch (...) {
                            // Ignore invalid values
                        }
                    }
                    
                    pos = (nextAmpPos == std::string::npos) ? queryString.length() : nextAmpPos + 1;
                }
            }

            // Already decoded, then a pending font whose hint says it is this one, then the
            // external loader, and only as a last resort every pending font. The sweep is what
            // decoding all of them up front used to do, and it is the expensive part - resolving
            // the fallback font ('Arial', which no style package carries) would trigger it on
            // every context build if it came before the external loader.
            auto fontDataIt = _fontDataMap.find(fontName);
            if (fontDataIt == _fontDataMap.end()) {
                fontDataIt = loadHintedFontData(fontName);
            }
            if (fontDataIt == _fontDataMap.end()) {
                fontDataIt = loadExternalFontData(fontName);
            }
            if (fontDataIt == _fontDataMap.end()) {
                fontDataIt = loadRemainingFontData(fontName);
                if (fontDataIt == _fontDataMap.end()) {
                    return std::shared_ptr<Font>();
                }
            }

            // Get existing glyph map or create new one (use full name with query params for caching)
            auto glyphMapIt = _glyphMapMap.find(name);
            if (glyphMapIt == _glyphMapMap.end()) {
                glyphMapIt = _glyphMapMap.emplace(name, std::make_shared<GlyphMap>(_maxGlyphMapWidth, _maxGlyphMapHeight)).first;
            }

            // Create new font
            auto font = std::make_shared<FontManagerFont>(_library, name, glyphMapIt->second, &fontDataIt->second, baseFont, glyphRenderSize);

            // Preload often-used characters
            std::vector<std::uint32_t> glyphPreloadTable;
            std::for_each(_glyphPreloadTable.begin(), _glyphPreloadTable.end(), [&glyphPreloadTable](char c) { glyphPreloadTable.push_back(c); });
            for (std::size_t i = 0; i < glyphPreloadTable.size(); i++) {
                font->shapeGlyphs(&glyphPreloadTable[i], 1, 1.0f, false);
            }

            // Cache the font
            _fontMap[std::make_pair(name, baseFont)] = font;
            return font;
        }

        // Note: _mutex is expected to be locked by the caller
        std::map<std::string, std::vector<unsigned char>>::iterator loadHintedFontData(const std::string& fontName) const {
            // One font answers the request, in the common case: the hint is what it is expected to
            // be called. It is only a hint - a font whose file says one thing and whose name table
            // says another is found by the sweep instead.
            std::string normalizedName = normalizeFontName(fontName);
            for (auto it = _pendingFonts.begin(); it != _pendingFonts.end(); it++) {
                if (it->hintName != normalizedName) {
                    continue;
                }
                FontDataProvider dataProvider = std::move(it->dataProvider);
                _pendingFonts.erase(it);
                loadFontDataUnlocked(dataProvider());
                return _fontDataMap.find(fontName);
            }
            return _fontDataMap.end();
        }

        // Note: _mutex is expected to be locked by the caller
        std::map<std::string, std::vector<unsigned char>>::iterator loadRemainingFontData(const std::string& fontName) const {
            // What eager loading did: decode everything left and register the names those fonts
            // actually carry. Only a package whose file names do not match its font names gets
            // here, and only once.
            if (_pendingFonts.empty()) {
                return _fontDataMap.end();
            }
            std::vector<PendingFont> pendingFonts;
            std::swap(pendingFonts, _pendingFonts);
            for (const PendingFont& pendingFont : pendingFonts) {
                loadFontDataUnlocked(pendingFont.dataProvider());
            }
            return _fontDataMap.find(fontName);
        }

        // Note: _mutex is expected to be locked by the caller
        std::map<std::string, std::vector<unsigned char>>::iterator loadExternalFontData(const std::string& fontName) const {
            if (!_fontDataLoader || _missingFontSet.find(fontName) != _missingFontSet.end()) {
                return _fontDataMap.end();
            }

            std::vector<unsigned char> data = _fontDataLoader(fontName);
            if (!data.empty()) {
                FontManagerLibrary library;
                FT_Face face = nullptr;
                int error = FT_New_Memory_Face(library.getLibrary(), data.data(), static_cast<FT_Long>(data.size()), 0, &face);
                if (error != 0) {
                    data.clear();
                }
                else {
                    FT_Done_Face(face);
                }
            }
            if (data.empty()) {
                _missingFontSet.insert(fontName);
                return _fontDataMap.end();
            }
            return _fontDataMap.emplace(fontName, std::move(data)).first;
        }

        static std::string readSfntName(const FT_SfntName& sfntName) {
            static const std::pair<int, int> be16Encodings[] = {
                { TT_PLATFORM_APPLE_UNICODE, TT_APPLE_ID_DEFAULT },
                { TT_PLATFORM_APPLE_UNICODE, TT_APPLE_ID_UNICODE_1_1 },
                { TT_PLATFORM_APPLE_UNICODE, TT_APPLE_ID_ISO_10646 },
                { TT_PLATFORM_APPLE_UNICODE, TT_APPLE_ID_UNICODE_2_0 },
                { TT_PLATFORM_ISO, TT_ISO_ID_10646 },
                { TT_PLATFORM_MICROSOFT, TT_MS_ID_UNICODE_CS },
                { TT_PLATFORM_MICROSOFT, TT_MS_ID_SYMBOL_CS },
                { -1, -1 }
            };

            for (int i = 0; be16Encodings[i].first != -1; i++) {
                if (be16Encodings[i].first == sfntName.platform_id && be16Encodings[i].second == sfntName.encoding_id) {
                    std::string name;
                    for (unsigned int j = 0; j < sfntName.string_len; j += 2) {
                        const char* c = reinterpret_cast<const char*>(sfntName.string) + j;
                        if (c[0] != 0) {
                            return std::string(); // simply ignore complex names
                        }
                        name.append(1, c[1]);
                    }
                    return name;
                }
            }
            return std::string(reinterpret_cast<const char*>(sfntName.string), sfntName.string_len);
        }

        static std::string normalizeFontName(const std::string& name) {
            std::string normalized;
            for (char c : name) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    normalized.append(1, static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
            }
            return normalized;
        }

        struct PendingFont {
            std::string hintName; // normalized
            FontDataProvider dataProvider;
        };

        const std::string _glyphPreloadTable = ""; // list of glyphs to preload when initializing
        const int _maxGlyphMapWidth;
        const int _maxGlyphMapHeight;
        FontDataLoader _fontDataLoader;
        mutable std::vector<PendingFont> _pendingFonts;
        mutable std::map<std::string, std::vector<unsigned char>> _fontDataMap;
        mutable std::set<std::string> _missingFontSet;
        std::shared_ptr<FontManagerLibrary> _library;
        mutable std::map<std::pair<std::string, std::shared_ptr<const Font>>, std::shared_ptr<FontManagerFont>> _fontMap;
        mutable std::map<std::string, std::shared_ptr<GlyphMap>> _glyphMapMap;
        mutable std::mutex _mutex;
    };

    FontManager::FontManager(int maxGlyphMapWidth, int maxGlyphMapHeight) : _impl(std::make_unique<Impl>(maxGlyphMapWidth, maxGlyphMapHeight)) {
    }

    FontManager::~FontManager() {
    }

    std::string FontManager::loadFontData(const std::vector<unsigned char>& data) {
        return _impl->loadFontData(data);
    }

    void FontManager::addPendingFontData(const std::string& hintName, FontDataProvider dataProvider) {
        _impl->addPendingFontData(hintName, std::move(dataProvider));
    }

    void FontManager::setFontDataLoader(FontDataLoader loader) {
        _impl->setFontDataLoader(std::move(loader));
    }

    std::shared_ptr<const Font> FontManager::getFont(const std::string& name, const std::shared_ptr<const Font>& baseFont) const {
        return _impl->getFont(name, baseFont);
    }

    std::shared_ptr<const Font> FontManager::getFont(const std::shared_ptr<const Font>& font, int glyphRenderSize) const {
        return _impl->getFont(font, glyphRenderSize);
    }
}
