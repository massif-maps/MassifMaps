/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FLATTENSWITCHTIMELINE_H_
#define _MASSIF_FLATTENSWITCHTIMELINE_H_

#include "terrain/FlattenSwitch.h"

#include <algorithm>

namespace massif {

    /**
     * How long a 2D/3D switch spent in each of its phases, so "the switch is slow" names one of
     * them instead of covering all three. Fed the switch's own phase once a frame, and free of the
     * renderer so the host tests can reach it. See docs/internals/rendering/04-terrain.md.
     *
     * The three costs a user sees are separate and have separate fixes: the WARM wait re-decodes
     * the visible tiles and is what makes the labels leave, the RAMP is the animation itself, and
     * the SETTLE afterwards is the drape catching up on a camera that has already landed.
     */
    struct FlattenSwitchTimeline {
        /** Content still arriving this long after the switch landed is reported anyway. */
        static constexpr float MAX_SETTLE_SECONDS = 30.0f;

        struct Report {
            bool rising = false;        // 2D -> 3D; the direction that waits for tiles
            float warmSeconds = 0.0f;
            float rampSeconds = 0.0f;
            float settleSeconds = 0.0f;
            int warmFrames = 0;
            int rampFrames = 0;
            int settleFrames = 0;
            int tilesOwed = 0;          // tiles the warm wait was still owed when it ended
            int bakes = 0;              // drape bakes the settle needed
            bool timedOut = false;      // the warm wait gave up rather than got its tiles

            float totalSeconds() const { return warmSeconds + rampSeconds + settleSeconds; }
        };

        struct Input {
            FlattenSwitch::Phase phase = FlattenSwitch::Phase::TERRAIN;
            float deltaSeconds = 0.0f;
            int tilesOwed = 0;         // summed over every layer the warm wait covers
            bool warmTimedOut = false;
            // Drape bakes the frame's budget could not get through. What "the mesh takes forever
            // to appear" is; the tiles themselves cannot be polled from here, see the renderer.
            bool bakesQueued = false;
            int bakes = 0;             // drape bakes this frame
        };

        /** Whether a switch is being timed. */
        bool isActive() const { return _active; }

        /**
         * Accumulates one frame. Returns true - once - on the frame the switch stops costing
         * anything, with 'report' filled in.
         */
        bool step(const Input& input, Report& report) {
            // The first frame only records where the switch already was: the renderer seeds its
            // state before stepping it, and a map that opens FLAT would otherwise read as a switch.
            FlattenSwitch::Phase was = _lastPhase;
            bool seeded = _seeded;
            _lastPhase = input.phase;
            _seeded = true;
            if (seeded && !_active && was != input.phase
                && (was == FlattenSwitch::Phase::FLAT || was == FlattenSwitch::Phase::TERRAIN)) {
                _active = true;
                _current = Report();
                _current.rising = was == FlattenSwitch::Phase::FLAT;
            }
            if (!_active) {
                return false;
            }

            float delta = std::max(0.0f, input.deltaSeconds);
            switch (input.phase) {
            case FlattenSwitch::Phase::WARMING:
                _current.warmSeconds += delta;
                _current.warmFrames++;
                _current.tilesOwed = input.tilesOwed;
                _current.timedOut = input.warmTimedOut;
                return false;
            case FlattenSwitch::Phase::RAMPING:
            case FlattenSwitch::Phase::MANUAL:
                _current.rampSeconds += delta;
                _current.rampFrames++;
                return false;
            case FlattenSwitch::Phase::FLAT:
            case FlattenSwitch::Phase::TERRAIN:
                break;
            }

            // Landed, and the drape is still baking - the part a user reads as "the mesh takes
            // forever to appear", rationed by the frame's bake budget rather than by the work.
            _current.settleSeconds += delta;
            _current.settleFrames++;
            _current.bakes += std::max(0, input.bakes);
            if (input.bakesQueued && _current.settleSeconds < MAX_SETTLE_SECONDS) {
                return false;
            }
            report = _current;
            _active = false;
            return true;
        }

    private:
        bool _active = false;
        bool _seeded = false;
        FlattenSwitch::Phase _lastPhase = FlattenSwitch::Phase::TERRAIN;
        Report _current;
    };

}

#endif
