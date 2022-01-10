/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TORQUEFEATUREDECODER_H_
#define _CARTO_MAPNIKVT_TORQUEFEATUREDECODER_H_

#include "FeatureDecoder.h"

#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include <cglib/bbox.h>
#include <cglib/mat.h>

namespace carto::mvt {
    class Logger;
    
    class TorqueFeatureDecoder : public FeatureDecoder {
    public:
        explicit TorqueFeatureDecoder(const std::vector<unsigned char>& data, int tileSize, int frameCount, const std::string& dataAggregation, const std::shared_ptr<Logger>& logger);

        void setTransform(const cglib::mat3x3<float>& transform);
        void setClipBox(const cglib::bbox2<float>& clipBox);

        std::shared_ptr<FeatureIterator> createFrameFeatureIterator(int frame, int frameOffset) const;

    private:
        class TorqueFeatureIterator;

        struct Element {
            int x;
            int y;
            double value;

            explicit Element(int x, int y, double value) : x(x), y(y), value(value) { }
        };

        std::unordered_map<int, std::vector<Element>> _timeValueMap;

        const int _tileSize;
        const int _frameCount;
        const std::string _dataAggregation;
        const std::shared_ptr<Logger> _logger;

        cglib::mat3x3<float> _transform = cglib::mat3x3<float>::identity();
        cglib::bbox2<float> _clipBox = cglib::bbox2<float>(cglib::vec2<float>(-0.125f, -0.125f), cglib::vec2<float>(1.125f, 1.125f));
    };
}

#endif
