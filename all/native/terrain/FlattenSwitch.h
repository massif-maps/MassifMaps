/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FLATTENSWITCH_H_
#define _MASSIF_FLATTENSWITCH_H_

#include <algorithm>

namespace massif {

    /**
     * The 2D/3D switch: how the map gets between flat and 3D terrain once something has asked for
     * the other one. AutoFlatten decides WHETHER to switch, this decides WHEN each half of it
     * happens. Free of the renderer and of TerrainOptions on purpose, so the host tests can reach
     * it. See TerrainOptions::setFlattenMode and docs/internals/rendering/04-terrain.md.
     *
     * The one fact the whole thing rests on: terrain-decoded tiles render correctly FLAT (they only
     * carry extra triangles), flat-decoded ones do NOT render correctly in 3D (no subdivision, so a
     * road chords straight over a valley). So every decode swap is made while the map is flat, where
     * both densities draw the same picture, and 3D is never entered before its tiles exist.
     */
    struct FlattenSwitch {
        enum class Phase {
            FLAT,      // flat, and decoded flat when the mode asks for it
            WARMING,   // 3D asked for: still rendering 2D while the tiles for it load
            RAMPING,   // the exaggeration ramp, either way
            TERRAIN,   // full 3D
            MANUAL     // the app owns the ratio, off its own clock
        };

        struct State {
            Phase phase = Phase::TERRAIN;
            float ratio = 0.0f;       // 0 full 3D, 1 flat
            bool decode3D = true;     // tiles carry terrain subdivision
            float warmSeconds = 0.0f; // time spent in WARMING, against the timeout
        };

        struct Input {
            bool flatten = false;     // what AutoFlatten or the app asked for
            bool manual = false;      // the app is driving the ratio itself
            float manualRatio = 0.0f;
            bool fullSwitch = false;  // FlattenMode FULL: the decode follows the flatten state
            bool tilesReady = false;  // every layer's visible set is loaded at the current density
            float deltaSeconds = 0.0f;
            float flattenDuration = 0.0f; // 3D -> 2D, 0 switches instantly
            float riseDuration = 0.0f;    // 2D -> 3D
            float warmTimeout = 0.0f; // give up waiting for the tiles after this; 0 waits forever
        };

        static State step(const State& state, const Input& input) {
            State next = state;
            float delta = std::max(0.0f, input.deltaSeconds);

            // The app taking the ratio pre-empts whatever the switch was doing; it gets it back by
            // writing Flattened again. The tile gate is NOT skipped - see below.
            if (input.manual) {
                next.phase = Phase::MANUAL;
            } else if (state.phase == Phase::MANUAL) {
                next.phase = state.ratio >= 1.0f ? Phase::FLAT
                           : state.ratio <= 0.0f ? Phase::TERRAIN
                                                 : Phase::RAMPING;
            }

            switch (next.phase) {
            case Phase::FLAT:
                next.ratio = 1.0f;
                if (!input.flatten) {
                    // Ask for the 3D tiles and keep rendering 2D until they are there. The LOD in
                    // terrain mode wants tiles flat rendering never asked for (overzoom targets,
                    // the coarsening floor), so this wait is not only about the decode density.
                    next.phase = Phase::WARMING;
                    next.warmSeconds = 0.0f;
                    next.decode3D = true;
                    break;
                }
                // Only once settled flat, and only in FULL mode, does the decode drop to 2D.
                next.decode3D = !input.fullSwitch;
                break;

            case Phase::WARMING:
                next.ratio = 1.0f;
                next.warmSeconds = state.warmSeconds + delta;
                if (input.flatten) {
                    next.phase = Phase::FLAT; // asked back before it ever left
                    break;
                }
                // The timeout is what keeps a tile that never loads from pinning the map in 2D:
                // late is better than never, and the ramp itself then shows what is missing.
                if (input.tilesReady || (input.warmTimeout > 0 && next.warmSeconds >= input.warmTimeout)) {
                    next.phase = Phase::RAMPING;
                }
                break;

            case Phase::RAMPING: {
                float target = input.flatten ? 1.0f : 0.0f;
                // Each direction has its own: the rise is the one an app matches to a flight, and
                // it is also the one that had to wait for its tiles first.
                float duration = input.flatten ? input.flattenDuration : input.riseDuration;
                float step = duration > 0 ? delta / duration : 1.0f;
                next.ratio = target > state.ratio ? std::min(target, state.ratio + step)
                                                  : std::max(target, state.ratio - step);
                if (next.ratio >= 1.0f) {
                    next.phase = Phase::FLAT;
                } else if (next.ratio <= 0.0f) {
                    next.phase = Phase::TERRAIN;
                }
                break;
            }

            case Phase::TERRAIN:
                next.ratio = 0.0f;
                next.decode3D = true;
                if (input.flatten) {
                    next.phase = Phase::RAMPING; // 3D tiles draw fine flat, so this way needs no wait
                }
                break;

            case Phase::MANUAL: {
                float asked = std::min(1.0f, std::max(0.0f, input.manualRatio));
                // Asking for any 3D asks for its tiles, and the ground is HELD flat until they are
                // there - the same gate WARMING is, because the same unsubdivided geometry would
                // otherwise be displaced over the relief. TerrainOptions::isSwitching is how an app
                // sees the hold rather than wondering why its ratio does nothing.
                next.decode3D = !input.fullSwitch || asked < 1.0f;
                bool held = asked < 1.0f && state.ratio >= 1.0f && !input.tilesReady;
                next.ratio = held ? 1.0f : asked;
                next.warmSeconds = 0.0f;
                break;
            }
            }

            return next;
        }

        /**
         * Whether 3D terrain is being rendered at all - the renderers, the cullers and the drape all
         * gate on this. WARMING is deliberately NOT active: it renders as plain 2D, so the wait
         * costs 2D and shows no half-built terrain.
         */
        static bool isTerrainActive(const State& state) {
            return state.ratio < 1.0f;
        }

        /**
         * Whether the switch is holding the ground flat while the tiles 3D needs load. What an app
         * driving the ratio itself waits on before it starts its own animation.
         */
        static bool isWaitingForTiles(const State& state, const Input& input) {
            if (state.phase == Phase::WARMING) {
                return true;
            }
            return state.phase == Phase::MANUAL && input.manualRatio < 1.0f && state.ratio >= 1.0f;
        }
    };

}

#endif
