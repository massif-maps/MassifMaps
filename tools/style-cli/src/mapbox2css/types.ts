export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };

export interface MapboxLayer {
    id: string;
    type: string;
    source?: string;
    'source-layer'?: string;
    minzoom?: number;
    maxzoom?: number;
    filter?: Json;
    layout?: Record<string, Json>;
    paint?: Record<string, Json>;
    /**
     * The style spec's own escape hatch, ignored by every renderer. `massif:paint` and
     * `massif:layout` inside it are merged over the real ones - see applyMassifExtras.
     */
    metadata?: Json;
    /**
     * Converter-internal, not a MapBox property: the zoom a banded dash reads its line width at.
     * Set by splitDashByZoom, read where `line-dasharray` is emitted.
     */
    dashZoom?: number;
}

export interface MapboxStyle {
    version?: number;
    name?: string;
    sprite?: string;
    glyphs?: string;
    sources?: Record<string, Json>;
    layers?: MapboxLayer[];
}

/** One entry of the generated allowlist (scripts/gen-cartocss-properties.py). */
export interface CartoProperty {
    cartocss: string;
    mapnik: string | null;
    symbolizer: string;
    kind: string;
    type: string;
    default: string;
    live: boolean;
    baked: boolean;
}

export interface PropertyTable {
    symbolizers: string[];
    properties: CartoProperty[];
}
