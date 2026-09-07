/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELCULLER_H_
#define _MASSIF_VT_LABELCULLER_H_

#include "ViewState.h"
#include "Label.h"

#include <array>
#include <functional>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include <mutex>

#include <cglib/vec.h>
#include <cglib/mat.h>
#include <cglib/bbox.h>

namespace massif::vt {
    class LabelCuller final {
    public:
        explicit LabelCuller(float scale);

        void setViewState(const ViewState& viewState);
        /**
         * Internal units per meter at the current view, so that a label style's max-distance
         * (which is in meters) can be compared against world-space distances. 0 disables the
         * test - a caller that does not set it gets the previous behaviour exactly.
         */
        void setMetersToInternal(double metersToInternal);
        void reset();
        bool process(const std::vector<std::shared_ptr<Label>>& labelList, std::mutex& labelMutex);

    private:
        static constexpr int GRID_RESOLUTION_X = 16;
        static constexpr int GRID_RESOLUTION_Y = 32;
        static constexpr float EXTRA_LABEL_BUFFER = 1.0f; // extra buffer for the label
        static constexpr float SCREEN_EDGE_MARGIN = 8.0f; // pixels a lifted callout keeps from the top edge
        static constexpr float AXIS_ALIGNED_EPSILON = 0.05f; // pixels an edge may drift and still count as straight
        static constexpr int LABEL_LOCK_BATCH = 32; // labels processed per acquisition of the label mutex


        struct CullRecord {
            cglib::bbox2<float> bounds;
            std::array<cglib::vec2<float>, 4> envelope;
            long long localId = 0;
            bool allowOverlapSameFeatureId = false;
            // The envelope is a screen-aligned rectangle, so its bounds ARE its shape and two such
            // records need no separating-axis test. True for every billboard label, whatever the
            // camera does - they face it.
            bool axisAligned = false;

            CullRecord() = default;
        };

        using RecordGrid = std::array<std::array<std::vector<CullRecord>, GRID_RESOLUTION_X>, GRID_RESOLUTION_Y>;

        struct LabelInfo {
            bool valid;
            bool wasVisible;
            float priority;
            int layerIndex;
            float size;
            float opacity;
            std::shared_ptr<Label> label;
            CullRecord cullRecord;
            // One per side the label may take, in preference order; the last one is its smallest
            // (the icon alone for a 'text-optional' shield). A label with one fixed layout has one.
            std::vector<CullRecord> variants;
        };

        cglib::vec2<int> getGridIndex(const cglib::vec2<float>& pos) const;
        void clearGrid();
        void addGridRecord(RecordGrid& grid, const CullRecord& cullRecord) const;
        bool testGridOverlap(const LabelInfo& labelInfo) const;
        // Whether two records whose bounds already intersect really overlap.
        static bool testRecordOverlap(const CullRecord& record1, const CullRecord& record2, float buffer);
        // Points the label at one of its layouts, and its cull record with it.
        static void takeVariant(LabelInfo& labelInfo, int index);
        // Fills the record's envelope, bounds and axisAligned flag from a world-space quad.
        void projectEnvelope(const std::array<cglib::vec3<float>, 4>& worldEnvelope, CullRecord& record) const;
        bool calculateScreenEnvelope(const std::shared_ptr<Label>& label, float size, CullRecord& record) const;
        // A CALLOUT label is lifted away from its anchor until it finds free screen space instead
        // of being hidden. Returns false when it ran out of rows. Updates the label's offset and
        // the cull record in place.
        bool placeCalloutLabel(LabelInfo& labelInfo, const std::function<bool(const LabelInfo&)>& testGroupDistance);
        // A label whose style names several sides (TextLabelStyle::anchors) takes the first free
        // one - tangram's 'do { ... } while (isOccluded() && nextAnchor())' (labelManager.cpp).
        // Returns false when no side is free. Updates the label's variant and the cull record.
        bool placeAnchoredLabel(LabelInfo& labelInfo, const std::function<bool(const LabelInfo&)>& testGroupDistance);

        cglib::mat4x4<float> _localCameraProjMatrix;
        ViewState _viewState;
        double _metersToInternal = 0;
        RecordGrid _recordGrid;

        const float _scale;

        mutable std::mutex _mutex;
    };
}

#endif
