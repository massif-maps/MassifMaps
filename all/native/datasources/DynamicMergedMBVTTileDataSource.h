/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DYNAMICMERGEDMBVTTILEDATASOURCE_H_
#define _MASSIF_DYNAMICMERGEDMBVTTILEDATASOURCE_H_

#include "datasources/TileDataSource.h"
#include "components/DirectorPtr.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace massif {

    /**
     * A tile data source merging a fixed base MBVT source with a mutable set of named MBVT sources (distinct layer ids),
     * so CompositeVectorTileLayer can add/remove merged sources while its own data source pointer stays constant.
     */
    class DynamicMergedMBVTTileDataSource : public TileDataSource {
    public:
        explicit DynamicMergedMBVTTileDataSource(const std::shared_ptr<TileDataSource>& baseDataSource);
        virtual ~DynamicMergedMBVTTileDataSource();

        /**
         * Adds (or replaces, if the key already exists) a named MBVT source to merge on top
         * of the base source.
         */
        void addDataSource(const std::string& key, const std::shared_ptr<TileDataSource>& dataSource);
        /**
         * Removes the named source. Returns true if a source was removed.
         */
        bool removeDataSource(const std::string& key);
        /**
         * Returns true if a source with the given key is registered.
         */
        bool containsDataSource(const std::string& key) const;

        virtual int getMinZoom() const;
        virtual int getMaxZoom() const;
        virtual MapBounds getDataExtent() const;

        virtual std::shared_ptr<TileData> loadTile(const MapTile& tile);

    protected:
        class DataSourceListener : public TileDataSource::OnChangeListener {
        public:
            explicit DataSourceListener(DynamicMergedMBVTTileDataSource& dataSource);
            virtual void onTilesChanged(bool removeTiles);
        private:
            DynamicMergedMBVTTileDataSource& _dataSource;
        };

        // Caller holds _mutex.
        void rebuildChain();

        const DirectorPtr<TileDataSource> _baseDataSource;
        std::vector<std::pair<std::string, std::shared_ptr<TileDataSource> > > _extraDataSources;
        std::shared_ptr<TileDataSource> _mergedChain;

        mutable std::recursive_mutex _mutex;

    private:
        std::shared_ptr<DataSourceListener> _dataSourceListener;
    };

}

#endif
