#ifndef _MASSIF_VT_GLBPARSER_H_
#define _MASSIF_VT_GLBPARSER_H_

#include "Model.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace massif::vt {
    // Decodes an embedded PNG/JPEG into a bottom-up RGBA bitmap, as vt bitmaps are stored.
    using ModelImageDecoder = std::function<std::shared_ptr<const Bitmap>(const unsigned char* data, std::size_t size)>;

    // Static meshes only: no skinning, animation, morph targets or required extension (Draco, meshopt).
    // Null and an error message for anything else.
    std::shared_ptr<Model> parseGLB(const unsigned char* data, std::size_t size, const ModelImageDecoder& imageDecoder, std::string& error);
}

#endif
