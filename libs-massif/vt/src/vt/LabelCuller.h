/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELCULLER_H_
#define _MASSIF_VT_LABELCULLER_H_

#include "ViewState.h"
#include "Label.h"
#include "LabelDistance.h"

#include <array>
#include <functional>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

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
        /**
         * How far a label may be from the camera, in multiples of the camera-to-focus distance.
         * Default LabelDistance::DEFAULT_VIEW_DISTANCE (5, as maplibre); 0 places every label.
         */
        void setLabelViewDistance(double viewDistance);
        /**
         * Whether a label's anchor is hidden by the terrain, applied during placement so a hidden
         * label reserves no collision slot (as mapbox's collision_index.ts). Empty = no test.
         */
        void setOcclusionTest(std::function<bool(const cglib::vec3<double>&)> test);
        void reset();
        /**
         * Opens a slice of a placement cycle, giving every process() call after it a shared
         * wall-clock budget. mapbox and maplibre both ration placement this way (2 ms, then resume
         * next frame from a cursor); a budget of 0 restores the un-rationed behaviour exactly.
         */
        void beginSlice(double budgetMs);
        /** Whether the last slice ran out of budget with labels still unvisited. */
        bool isSliceExhausted() const;
        /**
         * Places labels from `cursor` onward, advancing it. Stops early once the slice's budget is
         * spent, leaving the rest for the next pass - they keep the visibility they already had,
         * which is what mapbox's uncommitted placement amounts to.
         */
        bool process(const std::vector<std::shared_ptr<Label>>& labelList, std::mutex& labelMutex, std::size_t& cursor);

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
            // The boxes it collides by when the envelope claims more than it covers (Label::calculateVariantEnvelopes):
            // a name and its icon, a line run's glyphs. Empty = the envelope.
            std::vector<std::array<cglib::vec2<float>, 4>> parts;
            std::vector<cglib::bbox2<float>> partBounds;
            long long localId = 0;
            bool allowOverlapSameFeatureId = false;
            // The envelope is a screen-aligned rectangle, so its bounds ARE its shape and two such
            // records need no separating-axis test. True for every billboard label, whatever the
            // camera does - they face it.
            bool axisAligned = false;
            // In _reservedGrid: the insertion index of the label holding it.
            int reservation = -1;

            CullRecord() = default;
        };

        using RecordGrid = std::array<std::array<std::vector<CullRecord>, GRID_RESOLUTION_X>, GRID_RESOLUTION_Y>;

        struct LabelInfo {
            bool valid;
            bool wasVisible;
            bool occluded; // partly hidden by 3D content on the last frame drawn
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
        /** Placed, but not on screen yet - it skips the fade, so it is drawn the moment it scrolls in. */
        bool isOffscreen(const CullRecord& cullRecord) const;
        void clearGrid();
        void addGridRecord(RecordGrid& grid, const CullRecord& cullRecord) const;
        bool testGridOverlap(const LabelInfo& labelInfo) const;
        // Whether the record covers a label shown last pass that has not been inserted yet.
        bool testReservedOverlap(const CullRecord& cullRecord, int index) const;
        // Whether two records whose bounds, grown by the buffer, already intersect really overlap.
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
        // A name yields to a label shown last pass (see 06-labels.mdx), evicting only when nothing else fits.
        bool placeAnchoredLabel(LabelInfo& labelInfo, int index, const std::function<bool(const LabelInfo&)>& testGroupDistance);

        cglib::mat4x4<float> _localCameraProjMatrix;
        ViewState _viewState;
        double _metersToInternal = 0;
        double _labelViewDistance = LabelDistance::DEFAULT_VIEW_DISTANCE;
        // This pass's highest following-band callout anchor on screen (y up, resolution units), < 0 = none.
        float _highestCalloutAnchorY = -1.0f;
        cglib::vec3<double> _highestCalloutAnchorPosition = cglib::vec3<double>(0, 0, 0);
        std::function<bool(const cglib::vec3<double>&)> _occlusionTest;
        std::chrono::steady_clock::time_point _sliceDeadline;
        bool _sliceBudgeted = false;
        bool _sliceExhausted = false;
        RecordGrid _recordGrid;
        RecordGrid _reservedGrid; // this layer's labels shown last pass, by the side they held

        const float _scale;

        mutable std::mutex _mutex;
    };
}

#endif
