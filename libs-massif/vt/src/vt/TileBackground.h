/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILEBACKGROUND_H_
#define _CARTO_VT_TILEBACKGROUND_H_

#include "Bitmap.h"
#include "Color.h"

#include <memory>

namespace carto::vt {
    class TileBackground final {
    public:
        explicit TileBackground(const Color& color, std::shared_ptr<const BitmapPattern> pattern) : _color(color), _pattern(std::move(pattern)) { }

        const Color& getColor() const { return _color; }
        const std::shared_ptr<const BitmapPattern>& getPattern() const { return _pattern; }

        std::size_t getResidentSize() const {
            return 16 + sizeof(TileBackground);
        }

    private:
        const Color _color;
        const std::shared_ptr<const BitmapPattern> _pattern;
    };
}

#endif
