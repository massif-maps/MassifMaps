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
