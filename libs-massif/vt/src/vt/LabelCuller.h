/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_LABELCULLER_H_
#define _CARTO_VT_LABELCULLER_H_

#include "ViewState.h"
#include "Label.h"

#include <array>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include <mutex>

#include <cglib/vec.h>
#include <cglib/mat.h>
#include <cglib/bbox.h>

namespace carto::vt {
    class LabelCuller final {
    public:
        explicit LabelCuller(float scale);

        void setViewState(const ViewState& viewState);
        void reset();
        bool process(const std::vector<std::shared_ptr<Label>>& labelList, std::mutex& labelMutex);

    private:
        static constexpr int GRID_RESOLUTION_X = 16;
        static constexpr int GRID_RESOLUTION_Y = 32;
        static constexpr float EXTRA_LABEL_BUFFER = 1.0f; // extra buffer for the label

        struct CullRecord {
            cglib::bbox2<float> bounds;
            std::array<cglib::vec2<float>, 4> envelope;

            CullRecord() = default;
            explicit CullRecord(const cglib::bbox2<float>& bounds, const std::array<cglib::vec2<float>, 4>& envelope) : bounds(bounds), envelope(envelope) { }
        };

        cglib::vec2<int> getGridIndex(const cglib::vec2<float>& pos) const;
        void clearGrid();
        void addGridRecord(const CullRecord& cullRecord);
        bool testGridOverlap(const CullRecord& cullRecord) const;
        bool calculateScreenEnvelope(const std::shared_ptr<Label>& label, std::array<cglib::vec2<float>, 4>& envelope) const;

        cglib::mat4x4<float> _localCameraProjMatrix;
        ViewState _viewState;
        std::vector<CullRecord> _recordGrid[GRID_RESOLUTION_Y][GRID_RESOLUTION_X];

        const float _scale;

        mutable std::mutex _mutex;
    };
}

#endif
