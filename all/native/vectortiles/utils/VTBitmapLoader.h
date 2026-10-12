/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VTBITMAPLOADER_H_
#define _MASSIF_VTBITMAPLOADER_H_

#include "utils/URLFileLoader.h"

#include <string>
#include <vector>
#include <memory>

#include <vt/Bitmap.h>
#include <vt/BitmapManager.h>

namespace massif {
    class AssetPackage;
    
    class VTBitmapLoader : public vt::BitmapManager::BitmapLoader {
    public:
        VTBitmapLoader(const std::string& basePath, const std::shared_ptr<AssetPackage>& assetPackage);
        virtual ~VTBitmapLoader();
    
        virtual std::shared_ptr<const vt::Bitmap> load(const std::string& url, float& resolution) const;

        // PNG/JPEG/WebP to a bottom-up RGBA bitmap, as vt stores them; null if it does not decode.
        static std::shared_ptr<const vt::Bitmap> DecodeBitmap(const unsigned char* data, std::size_t size);
    
    private:
        std::shared_ptr<const vt::Bitmap> loadSVG(const std::vector<unsigned char>& fileData, float& resolution) const;

        std::string _basePath;
        std::shared_ptr<AssetPackage> _assetPackage;
        URLFileLoader _urlFileLoader;
    };
    
}

#endif
