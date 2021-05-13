/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_FEATUREDECODER_H_
#define _CARTO_MAPNIKVT_FEATUREDECODER_H_

#include "Feature.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>

namespace carto { namespace mvt {
    class FeatureDecoder {
    public:
        class FeatureIterator {
        public:
            virtual ~FeatureIterator() = default;

            virtual bool valid() const = 0;
            virtual void advance() = 0;

            virtual long long getLocalId() const = 0;
            virtual long long getGlobalId() const = 0;
            virtual std::shared_ptr<const Geometry> getGeometry() const = 0;
            virtual std::shared_ptr<const FeatureData> getFeatureData(const std::set<std::string>* fields) const = 0;
        };

        virtual ~FeatureDecoder() = default;

    protected:
        class GeometryCache {
        public:
            GeometryCache() = default;

            void reserve(std::size_t size) {
                std::lock_guard<std::mutex> lock(_mutex);
                _container.reserve(size);
            }

            std::shared_ptr<const Geometry> get(std::size_t index) const {
                std::lock_guard<std::mutex> lock(_mutex);
                if (index < _container.size()) {
                    return _container[index];
                }
                return std::shared_ptr<const Geometry>();
            }

            void put(std::size_t index, std::shared_ptr<const Geometry> geometry) {
                std::lock_guard<std::mutex> lock(_mutex);
                if (index >= _container.size()) {
                    _container.resize(index + 1);
                }
                _container[index] = std::move(geometry);
            }

        private:
            std::vector<std::shared_ptr<const Geometry>> _container;
            mutable std::mutex _mutex;
        };

        template <typename T>
        class FeatureDataCache {
        public:
            FeatureDataCache() = default;

            void reserve(std::size_t size) {
            }

            std::shared_ptr<const FeatureData> get(const T& key) const {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _container.find(key);
                if (it != _container.end()) {
                    return it->second;
                }
                return std::shared_ptr<const FeatureData>();
            }

            void put(T key, std::shared_ptr<const FeatureData> data) {
                std::lock_guard<std::mutex> lock(_mutex);
                _container.emplace(std::move(key), std::move(data));
            }

        private:
            std::map<T, const std::shared_ptr<const FeatureData>> _container;
            mutable std::mutex _mutex;
        };
    };
} }

#endif
