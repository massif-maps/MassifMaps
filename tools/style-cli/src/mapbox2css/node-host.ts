/**
 * The CLI's sprite host: `node:fs` and pngjs, which is what this file exists to keep OUT of
 * sprite.ts — a browser bundle of the converter imports neither.
 */

import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { PNG } from 'pngjs';

import type { PngEncoding, RgbaImage, SpriteHost } from './host.js';

export const nodeSpriteHost: SpriteHost = {
    async fetchBytes(url: string): Promise<Uint8Array> {
        if (!/^https?:/.test(url)) return readFileSync(url);
        const response = await fetch(url);
        if (!response.ok) throw new Error(`${response.status} ${response.statusText} for ${url}`);
        return new Uint8Array(await response.arrayBuffer());
    },

    async decodePng(bytes: Uint8Array): Promise<RgbaImage> {
        const png = PNG.sync.read(Buffer.from(bytes));
        return { width: png.width, height: png.height, data: png.data };
    },

    encodePng(image: RgbaImage, encoding: PngEncoding): Uint8Array {
        const png = new PNG({ width: image.width, height: image.height });
        png.data = Buffer.from(image.data.buffer, image.data.byteOffset, image.data.length);
        return PNG.sync.write(png, {
            deflateLevel: 9,
            filterType: -1,
            ...(encoding.greyscale ? { colorType: 0 } : {}),
        });
    },

    writeIcon(outDir: string, file: string, bytes: Uint8Array): void {
        const target = join(outDir, ...file.split('/'));
        mkdirSync(dirname(target), { recursive: true });
        writeFileSync(target, bytes);
    },
};
