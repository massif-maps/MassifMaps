/**
 * What the sprite slicer needs from the world outside it: bytes in, PNGs decoded and encoded,
 * files out.
 *
 * The conversion itself is plain data work and runs anywhere, but sprites are the one part that
 * reads the network and writes files. Behind this interface the CLI keeps using `node:fs` and
 * `pngjs`, and the website's style preview uses `fetch` and a canvas — which is what lets the
 * whole converter be bundled for a browser, since neither of those two imports survives there.
 */

/** An 8-bit RGBA image: exactly the part of pngjs's `PNG` the slicer ever touches. */
export interface RgbaImage {
    width: number;
    height: number;
    data: Uint8Array;
}

export interface PngEncoding {
    /**
     * Write one channel instead of four. A distance field is a single channel, and the SDK reads
     * it from red, so this is a pure size win — the sprite is most of what a converted style
     * weighs. A host that cannot do it (a canvas cannot) writes RGBA and is only bigger.
     */
    greyscale?: boolean;
}

export interface SpriteHost {
    /** A sprite sheet's bytes. The CLI accepts a local path here; a browser only ever has URLs. */
    fetchBytes(url: string): Promise<Uint8Array>;
    decodePng(bytes: Uint8Array): Promise<RgbaImage>;
    encodePng(image: RgbaImage, encoding: PngEncoding): Uint8Array;
    /** Writes one icon at `file` — a relative path with `/` separators — under `outDir`. */
    writeIcon(outDir: string, file: string, bytes: Uint8Array): void;
}

export function createImage(width: number, height: number): RgbaImage {
    return { width, height, data: new Uint8Array(width * height * 4) };
}
