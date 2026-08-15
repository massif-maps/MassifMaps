/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_TILEBACKGROUND_H_
#define _MASSIF_VT_TILEBACKGROUND_H_

#include "Bitmap.h"
#include "Styles.h"

#include <memory>

namespace massif::vt {
    class TileBackground final {
    public:
        explicit TileBackground(ColorFunction colorFunc, std::shared_ptr<const BitmapPattern> pattern) : _colorFunc(std::move(colorFunc)), _pattern(std::move(pattern)) { }

        const ColorFunction& getColorFunc() const { return _colorFunc; }
        const std::shared_ptr<const BitmapPattern>& getPattern() const { return _pattern; }

        std::size_t getResidentSize() const {
            return 16 + sizeof(TileBackground);
        }

    private:
        const ColorFunction _colorFunc;
        const std::shared_ptr<const BitmapPattern> _pattern;
    };
}

#endif
