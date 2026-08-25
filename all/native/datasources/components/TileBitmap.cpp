#include "TileBitmap.h"
#include "TileData.h"
#include "core/BinaryData.h"
#include "graphics/Bitmap.h"
#include "utils/Log.h"

namespace massif {

    std::shared_ptr<Bitmap> DecodeTileBitmap(const std::shared_ptr<TileData>& tileData) {
        if (!tileData) {
            return std::shared_ptr<Bitmap>();
        }
        std::shared_ptr<BinaryData> data = tileData->getData();
        if (!data || data->empty()) {
            return std::shared_ptr<Bitmap>();
        }

        if (tileData->isRawPixels()) {
            std::size_t width = static_cast<std::size_t>(tileData->getWidth());
            std::size_t height = static_cast<std::size_t>(tileData->getHeight());
            // Checked, not trusted: the size comes from whatever produced the tile, and a short
            // buffer here is a read past the end inside the texture upload.
            if (data->size() < width * height * 4) {
                Log::Errorf("DecodeTileBitmap: %dx%d raw tile needs %d bytes, got %d",
                            tileData->getWidth(), tileData->getHeight(),
                            static_cast<int>(width * height * 4), static_cast<int>(data->size()));
                return std::shared_ptr<Bitmap>();
            }
            return std::make_shared<Bitmap>(data, static_cast<unsigned int>(width),
                                            static_cast<unsigned int>(height),
                                            ColorFormat::COLOR_FORMAT_RGBA,
                                            static_cast<int>(width * 4));
        }

        std::shared_ptr<Bitmap> bitmap = Bitmap::CreateFromCompressed(data);
        if (!bitmap) {
            Log::Error("DecodeTileBitmap: Failed to decode tile");
        }
        return bitmap;
    }

}
