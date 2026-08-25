/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_CALLBACKTILEDATASOURCE_H_
#define _MASSIF_API_CALLBACKTILEDATASOURCE_H_

#include "api/MassifApiC.h"
#include "datasources/TileDataSource.h"

namespace massif { namespace api {

    /**
     * A tile source whose tiles come from a C function pointer.
     *
     * What mm_source_create_custom builds. The point is that an EXTENSION - another shared
     * library, in any language with a C FFI - can supply tiles without the SDK knowing the format
     * or linking the library that reads it. The binding path (a SWIG director subclassed in Java)
     * does the same job for a managed language; this is the one for native code, which cannot
     * subclass massif::TileDataSource because the SDK exports no C++ symbols.
     *
     * The callback runs on the SDK's tile threads, several at once, so it must be thread-safe -
     * nothing here serialises it.
     */
    class CallbackTileDataSource : public TileDataSource {
    public:
        CallbackTileDataSource(int minZoom, int maxZoom, const mm_tile_source& source);
        virtual ~CallbackTileDataSource();

        virtual std::shared_ptr<TileData> loadTile(const MapTile& tile);

        /**
         * Drops the destroy callback, for a source that was built and then not handed over.
         * mm_source_create_custom promises that a failed create takes nothing.
         */
        void disown();

    private:
        const mm_tile_loader _loadTile;
        void (*_destroy)(void*);
        void* const _userData;
    };

} }

#endif
