#include "GlyphMap.h"

#include <atomic>
#include <mutex>
#include <memory>
#include <array>
#include <map>
#include <unordered_map>
#include <algorithm>

namespace massif::vt {
    GlyphMap::GlyphMap(int maxWidth, int maxHeight) : _maxWidth(maxWidth), _maxHeight(maxHeight) {
        _glyphMap[0] = std::make_unique<Glyph>(GlyphMode::BACKGROUND, 0, 0, 0, 0, cglib::vec2<float>(0, 0));
    }

    const GlyphMap::Glyph* GlyphMap::getGlyph(GlyphId glyphId) const {
        std::lock_guard<std::mutex> lock(_mutex);
        
        auto it = _glyphMap.find(glyphId);
        if (it == _glyphMap.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    GlyphMap::GlyphId GlyphMap::loadBitmapGlyph(const std::shared_ptr<const Bitmap>& bitmap, GlyphMode mode) {
        if (!bitmap) {
            return 0;
        }

        cglib::vec2<float> origin(bitmap->width * 0.5f, bitmap->height * 0.5f);
        return loadBitmapGlyph(bitmap, mode, origin);
    }
    
    GlyphMap::GlyphId GlyphMap::loadBitmapGlyph(const std::shared_ptr<const Bitmap>& bitmap, GlyphMode mode, const cglib::vec2<float>& origin) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!bitmap) {
            return 0;
        }

        auto it = _bitmapGlyphMap.find(bitmap);
        if (it != _bitmapGlyphMap.end()) {
            return it->second;
        }

        if (bitmap->width + 2 > _maxWidth) {
            return 0;
        }
        if (_buildState.x0 + bitmap->width + 2 > _maxWidth) {
            _buildState.y0 = _buildState.y1;
            _buildState.x0 = 0;
        }
        if (_buildState.y0 + bitmap->height + 2 > _maxHeight) {
            return 0;
        }

        _buildState.x1 = std::max(_buildState.x1, _buildState.x0 + bitmap->width + 2);
        _buildState.y1 = std::max(_buildState.y1, _buildState.y0 + bitmap->height + 2);
        if (_buildState.y1 * _maxWidth > static_cast<int>(_buildState.bitmapData.size())) {
            _buildState.bitmapData.resize((_buildState.y1 + 16) * _maxWidth);
        }

        for (int y = 0; y < bitmap->height; y++) {
            const std::uint32_t* row = &bitmap->data[y * bitmap->width];
            std::copy(row, row + bitmap->width, &_buildState.bitmapData[(_buildState.y0 + y + 1) * _maxWidth + _buildState.x0 + 1]);
        }

        GlyphId glyphId = static_cast<GlyphId>(_glyphMap.size());
        _glyphMap[glyphId] = std::make_unique<Glyph>(mode, _buildState.x0 + 1, _buildState.y0 + 1, bitmap->width, bitmap->height, origin);
        _bitmapGlyphMap[bitmap] = glyphId;

        _buildState.x0 += bitmap->width + 2;

        _version++;
        if (_rowVersions.size() < static_cast<std::size_t>(_buildState.y1)) {
            _rowVersions.resize(_buildState.y1, 0);
        }
        std::fill(_rowVersions.begin() + _buildState.y0, _rowVersions.begin() + _buildState.y0 + bitmap->height + 2, _version);

        _bitmapPattern.reset();

        return glyphId;
    }

    std::shared_ptr<const BitmapPattern> GlyphMap::getBitmapPattern() const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_bitmapPattern) {
            int width = 1;
            while (width < _buildState.x1) { width *= 2; }
            int height = 1;
            while (height < _buildState.y1) { height *= 2; }

            std::vector<std::uint32_t> data(width * height);
            for (int y = 0; y < _buildState.y1; y++) {
                std::copy(&_buildState.bitmapData[y * _maxWidth], &_buildState.bitmapData[y * _maxWidth] + _buildState.x1, &data[y * width]);
            }

            _bitmapPattern = std::make_shared<BitmapPattern>(1.0f, 1.0f, std::make_shared<Bitmap>(width, height, std::move(data)));
        }

        return _bitmapPattern;
    }

    bool GlyphMap::getAtlasUpdate(std::uint64_t knownVersion, int knownWidth, int knownHeight, AtlasUpdate& update) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (knownWidth > 0 && knownVersion == _version) {
            return false;
        }

        int width = 1;
        while (width < _buildState.x1) { width *= 2; }
        int height = 1;
        while (height < _buildState.y1) { height *= 2; }

        update.version = _version;
        update.width = width;
        update.height = height;
        update.full = (width != knownWidth || height != knownHeight);
        update.y0 = 0;
        update.y1 = height;
        if (!update.full) {
            update.y0 = height;
            update.y1 = 0;
            for (std::size_t y = 0; y < _rowVersions.size(); y++) {
                if (_rowVersions[y] > knownVersion) {
                    update.y0 = std::min(update.y0, static_cast<int>(y));
                    update.y1 = static_cast<int>(y) + 1;
                }
            }
            update.y1 = std::max(update.y1, update.y0);
        }

        update.texels.assign(static_cast<std::size_t>(width) * (update.y1 - update.y0), 0);
        for (int y = update.y0; y < std::min(update.y1, _buildState.y1); y++) {
            std::copy(&_buildState.bitmapData[y * _maxWidth], &_buildState.bitmapData[y * _maxWidth] + _buildState.x1, &update.texels[static_cast<std::size_t>(y - update.y0) * width]);
        }
        return true;
    }
}
