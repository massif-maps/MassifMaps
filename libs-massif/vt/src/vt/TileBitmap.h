/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TILEBITMAP_H_
#define _CARTO_VT_TILEBITMAP_H_

#include <cstdint>
#include <vector>

namespace carto { namespace vt {
    class TileBitmap final {
    public:
        enum Type {
            COLORMAP, NORMALMAP
        };
        
        enum class Format {
            GRAYSCALE, RGB, RGBA
        };

        explicit TileBitmap(Type type, Format format, int width, int height, std::vector<std::uint8_t> data) : _type(type), _format(format), _width(width), _height(height), _data(std::move(data)) { }

        Type getType() const { return _type; }
        Format getFormat() const { return _format; }
        int getWidth() const { return _width; }
        int getHeight() const { return _height; }
        const std::vector<std::uint8_t>& getData() const { return _data; }

        void releaseBitmap() {
            _data.clear();
            _data.shrink_to_fit();
        }

        std::size_t getResidentSize() const {
            return 16 + _data.size();
        }

    private:
        const Type _type;
        const Format _format;
        const int _width;
        const int _height;
        
        std::vector<std::uint8_t> _data;
    };
} }

#endif
