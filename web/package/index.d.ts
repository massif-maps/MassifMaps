import type { MassifMap } from '@massif-maps/api';

export * from '@massif-maps/api';

export interface CreateMapOptions {
    /** The registry id the map's options take. Defaults to `map`. */
    id?: string;
    /** What positions read and write in. Defaults to `EPSG:4326`, plain `[lon, lat]`. */
    projection?: string;
    /** `full` loads massif-web-full.mjs, which adds routing, geocoding and offline packages. */
    variant?: 'full';
    /** Where massif-web.mjs is. Defaults to beside this package's own files; wins over `variant`. */
    moduleUrl?: string;
    /** Anything else goes to the emscripten module factory (print, printErr, locateFile...). */
    [option: string]: unknown;
}

/** Loads the SDK module and starts a map on `canvas`. One per page. */
export declare function createMap(canvas: HTMLCanvasElement, options?: CreateMapOptions): Promise<MassifMap & { module: any }>;
