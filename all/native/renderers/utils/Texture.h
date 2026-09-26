/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TEXTURE_H_
#define _MASSIF_TEXTURE_H_

#include "renderers/utils/GLResource.h"

#include <memory>

#include <cglib/vec.h>

namespace massif {
    class Bitmap;
    
    class Texture : public GLResource {
    public:
        virtual ~Texture();
        
        
        
        std::size_t getSize() const;
        
        const cglib::vec2<float>& getTexCoordScale() const;

        GLuint getTexId() const;

        /**
         * Uploads a sub-rectangle in the texture's own format; no-op before creation, no mipmap update.
         * The caller must update the source bitmap too: a context loss rebuilds the texture from it.
         */
        void updateSubImage(int x, int y, int width, int height, const unsigned char* data);

    protected:
        friend GLResourceManager;

        Texture(const std::weak_ptr<GLResourceManager>& manager, const std::shared_ptr<Bitmap>& bitmap, bool genMipmaps, bool repeat);

        virtual void create();
        virtual void destroy();

    private:
        static const int MAX_ANISOTROPY;
        
        static const double MIPMAP_SIZE_MULTIPLIER;
    
        static GLuint LoadFromBitmap(const Bitmap& bitmap, bool genMipmaps, bool repeat);
        
        std::shared_ptr<Bitmap> _bitmap;
        bool _mipmaps;
        bool _repeat;
        std::size_t _sizeInBytes;
        cglib::vec2<float> _texCoordScale;
    
        GLuint _texId;
    };
    
}

#endif
