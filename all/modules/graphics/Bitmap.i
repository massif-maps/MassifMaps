#ifndef _BITMAP_I
#define _BITMAP_I

%module Bitmap

!proxy_imports(massif::Bitmap, core.BinaryData)

%{
#include "graphics/Bitmap.h"
#include "components/Exceptions.h"
#include <memory>
%}

%include <std_shared_ptr.i>
%include <std_string.i>
%include <massifswig.i>

%import "core/BinaryData.i"

!enum(massif::ColorFormat::ColorFormat)
!shared_ptr(massif::Bitmap, graphics.Bitmap)

// No builder: a Bitmap comes from CreateFromCompressed, not a constructor - see
// SpecFactories.cpp. This only says which kind builds one, so an inline `{"url": …}`
// resolves wherever a Bitmap property is writable.
!spec(massif::Bitmap, bitmap, -)

%attribute(massif::Bitmap, unsigned int, Width, getWidth)
%attribute(massif::Bitmap, unsigned int, Height, getHeight)
%attribute(massif::Bitmap, massif::ColorFormat::ColorFormat, ColorFormat, getColorFormat)
%attribute(massif::Bitmap, unsigned int, BytesPerPixel, getBytesPerPixel)
%std_exceptions(massif::Bitmap::Bitmap)
%std_exceptions(massif::Bitmap::CreateFromCompressed)
%ignore massif::Bitmap::Bitmap(const unsigned char*, std::size_t);
%ignore massif::Bitmap::Bitmap(const unsigned char*, unsigned int, unsigned int, ColorFormat::ColorFormat, int);
%ignore massif::Bitmap::getPixelData;
%rename(getPixelData) massif::Bitmap::getPixelDataPtr;
%ignore massif::Bitmap::CreateFromCompressed(const unsigned char*, std::size_t);
!standard_equals(massif::Bitmap);

%include "graphics/Bitmap.h"

#endif
