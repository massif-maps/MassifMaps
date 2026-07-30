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

        explicit FontManager(int maxGlyphMapWidth, int maxGlyphMapHeight);
        virtual ~FontManager();

        std::string loadFontData(const std::vector<unsigned char>& data);
        void setFontDataLoader(FontDataLoader loader);
        std::shared_ptr<const Font> getFont(const std::string& name, const std::shared_ptr<const Font>& baseFont) const;

    private:
        class Impl;

        std::unique_ptr<Impl> _impl;
    };
}

#endif
