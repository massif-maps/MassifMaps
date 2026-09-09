/**
 * A sprite host for a browser: `fetch` for the sheets, the codec in png.ts, and a caller-supplied
 * sink for the icons.
 *
 * The sink is what makes this useful rather than academic. The website's style preview writes into
 * the wasm module's in-memory filesystem, so a converted style becomes a directory the SDK opens as
 * an asset package - the same shape the CLI writes on disk, without a disk.
 */

import type { PngEncoding, RgbaImage, SpriteHost } from './host.js';
import { decodePng, encodePng } from './png.js';

/** Where a converted style's icons go. `file` is relative, with `/` separators. */
export type IconSink = (outDir: string, file: string, bytes: Uint8Array) => void;

export function browserSpriteHost(writeIcon: IconSink): SpriteHost {
    return {
        async fetchBytes(url: string): Promise<Uint8Array> {
            const response = await fetch(url);
            if (!response.ok) throw new Error(`${response.status} ${response.statusText} for ${url}`);
            return new Uint8Array(await response.arrayBuffer());
        },

        decodePng(bytes: Uint8Array): Promise<RgbaImage> {
            return decodePng(bytes);
        },

        encodePng(image: RgbaImage, encoding: PngEncoding): Uint8Array {
            return encodePng(image, encoding);
        },

        writeIcon,
    };
}
