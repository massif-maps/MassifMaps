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
     * A tile source whose tiles come from a C function pointer (mm_source_create_custom), for native
     * extensions: the SDK exports no C++ symbols to subclass TileDataSource with.
     * The callback runs on several tile threads at once and must be thread-safe.
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
