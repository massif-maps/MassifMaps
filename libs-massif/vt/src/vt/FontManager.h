/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_FONTMANAGER_H_
#define _CARTO_VT_FONTMANAGER_H_

#include "Color.h"
#include "Font.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace carto::vt {
    class FontManager {
    public:
        // Called when a font name is not among the loaded fonts. Must return the font file data or an empty
        // vector if the name can not be resolved. Must not call back into the font manager.
        using FontDataLoader = std::function<std::vector<unsigned char>(const std::string& name)>;
        // Supplies the data of a font registered with addPendingFontData, when it is first needed.
        using FontDataProvider = std::function<std::vector<unsigned char>()>;

        explicit FontManager(int maxGlyphMapWidth, int maxGlyphMapHeight);
        virtual ~FontManager();

        std::string loadFontData(const std::vector<unsigned char>& data);
        // Registers a font decoded only once a name it may answer to is asked for - a woff2 must be
        // decompressed to be read at all (1-11 ms on a mid-range phone) and a style packs far more
        // fonts than it uses. 'hintName' is what the font is expected to be called (the file name):
        // a request normalizing to it takes that font alone, and one matching no hint goes to the
        // FontDataLoader before every pending font is decoded to register its real names.
        void addPendingFontData(const std::string& hintName, FontDataProvider dataProvider);
        void setFontDataLoader(FontDataLoader loader);
        std::shared_ptr<const Font> getFont(const std::string& name, const std::shared_ptr<const Font>& baseFont) const;
        // The same font (and the same fallback chain) rasterized at another glyph render size. A
        // font whose name declares a size of its own is returned as it is - see pickGlyphRenderSize.
        std::shared_ptr<const Font> getFont(const std::shared_ptr<const Font>& font, int glyphRenderSize) const;

    private:
        class Impl;

        std::unique_ptr<Impl> _impl;
    };
}

#endif
