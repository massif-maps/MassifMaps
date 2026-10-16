#ifndef _MASSIF_VT_ARROWHEADPASS_H_
#define _MASSIF_VT_ARROWHEADPASS_H_

namespace massif::vt {
    /** Which slots of a line batch a 2D draw paints (03-vt-renderer.md, "Line end arrows").
     *  The heads of `line-arrow-only` styles belong to one tile (the one holding the line's end) but
     *  overhang into its neighbours, so they are drawn once every tile of the style layer is done,
     *  unmasked: the tile mask cut them at the tile edge, and a neighbour's shaft drawn later painted
     *  over the part of the head lying in its tile. */
    enum class ArrowHeadPass {
        ALL,    // every slot, one draw per tile: no arrow-only slot in this style layer
        SHAFTS, // every slot but the arrow-only ones
        HEADS   // the arrow-only slots only
    };

    inline bool arrowSlotDrawn(ArrowHeadPass pass, bool arrowOnly) {
        return pass == ArrowHeadPass::ALL || (pass == ArrowHeadPass::HEADS) == arrowOnly;
    }

    /** Passes over a style layer's tiles: a second one for the heads, unless the layer is drawn in
     *  another scheme that owns the order (an overlay, a draw-once group). */
    inline int arrowHeadPassCount(bool anyArrowOnlySlot, bool plainLayer) {
        return anyArrowOnlySlot && plainLayer ? 2 : 1;
    }
}

#endif
