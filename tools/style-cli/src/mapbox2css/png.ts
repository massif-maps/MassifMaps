/*
 * A small, exact PNG codec, for the browser sprite host.
 *
 * The obvious way to read a sprite sheet in a browser is `createImageBitmap` into a 2D canvas, and
 * it is wrong here: a 2D context stores premultiplied colour, so `getImageData` gives back
 * un-premultiplied values that no longer match what the file held. Every partially transparent
 * texel is off by a little, and a sprite sheet is mostly edges - an SDF sheet, whose alpha channel
 * is a distance field rather than opacity, would be mangled outright.
 *
 * So the bytes are read as bytes. Inflate is the platform's own (`DecompressionStream`, in every
 * modern browser and in node); deflate on the way out is not needed at all, because these files go
 * into the wasm module's in-memory filesystem and are read straight back by the SDK - never sent
 * anywhere - so they are written as stored, uncompressed blocks. Bigger file, no dependency.
 *
 * The CLI keeps using pngjs, which compresses; this is only for the host that cannot.
 */

import { type RgbaImage, createImage } from './host.js';

const SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        table[n] = c >>> 0;
    }
    return table;
})();

function crc32(bytes: Uint8Array): number {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

function adler32(bytes: Uint8Array): number {
    let a = 1;
    let b = 0;
    for (let i = 0; i < bytes.length; i++) {
        a = (a + bytes[i]) % 65521;
        b = (b + a) % 65521;
    }
    return ((b << 16) | a) >>> 0;
}

function paeth(a: number, b: number, c: number): number {
    const p = a + b - c;
    const pa = Math.abs(p - a);
    const pb = Math.abs(p - b);
    const pc = Math.abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

/** Undoes the per-scanline filter PNG applies before compressing. */
function unfilter(raw: Uint8Array, width: number, height: number, bytesPerPixel: number): Uint8Array {
    const stride = width * bytesPerPixel;
    const out = new Uint8Array(stride * height);
    let source = 0;
    for (let y = 0; y < height; y++) {
        const filter = raw[source++];
        const row = y * stride;
        const previous = row - stride;
        for (let x = 0; x < stride; x++) {
            const value = raw[source++];
            const a = x >= bytesPerPixel ? out[row + x - bytesPerPixel] : 0;
            const b = y > 0 ? out[previous + x] : 0;
            const c = x >= bytesPerPixel && y > 0 ? out[previous + x - bytesPerPixel] : 0;
            switch (filter) {
                case 0: out[row + x] = value; break;
                case 1: out[row + x] = (value + a) & 0xff; break;
                case 2: out[row + x] = (value + b) & 0xff; break;
                case 3: out[row + x] = (value + ((a + b) >> 1)) & 0xff; break;
                case 4: out[row + x] = (value + paeth(a, b, c)) & 0xff; break;
                default: throw new Error(`PNG: unknown row filter ${filter}`);
            }
        }
    }
    return out;
}

async function inflate(bytes: Uint8Array): Promise<Uint8Array> {
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
}

const CHANNELS: Record<number, number> = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 };

/** Decodes a PNG to 8-bit RGBA. Non-interlaced, 8 bits per channel - what a sprite sheet is. */
export async function decodePng(bytes: Uint8Array): Promise<RgbaImage> {
    for (let i = 0; i < SIGNATURE.length; i++) {
        if (bytes[i] !== SIGNATURE[i]) throw new Error('PNG: not a PNG');
    }
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let offset = 8;
    let header: { width: number; height: number; depth: number; colorType: number; interlace: number } | null = null;
    let palette: Uint8Array | null = null;
    let transparency: Uint8Array | null = null;
    const parts: Uint8Array[] = [];
    while (offset + 8 <= bytes.length) {
        const length = view.getUint32(offset);
        const type = String.fromCharCode(bytes[offset + 4], bytes[offset + 5], bytes[offset + 6], bytes[offset + 7]);
        const body = bytes.subarray(offset + 8, offset + 8 + length);
        if (type === 'IHDR') {
            header = {
                width: view.getUint32(offset + 8),
                height: view.getUint32(offset + 12),
                depth: bytes[offset + 16],
                colorType: bytes[offset + 17],
                interlace: bytes[offset + 20],
            };
        } else if (type === 'PLTE') {
            palette = body.slice();
        } else if (type === 'tRNS') {
            transparency = body.slice();
        } else if (type === 'IDAT') {
            parts.push(body);
        } else if (type === 'IEND') {
            break;
        }
        offset += 12 + length;
    }
    if (!header) throw new Error('PNG: no IHDR');
    if (header.depth !== 8) throw new Error(`PNG: only 8 bits per channel, got ${header.depth}`);
    if (header.interlace !== 0) throw new Error('PNG: interlaced images are not read');
    const channels = CHANNELS[header.colorType];
    if (!channels) throw new Error(`PNG: unknown colour type ${header.colorType}`);

    let total = 0;
    for (const part of parts) total += part.length;
    const compressed = new Uint8Array(total);
    let at = 0;
    for (const part of parts) {
        compressed.set(part, at);
        at += part.length;
    }
    const raw = unfilter(await inflate(compressed), header.width, header.height, channels);

    const image = createImage(header.width, header.height);
    const { data } = image;
    for (let i = 0; i < header.width * header.height; i++) {
        const source = i * channels;
        const target = i * 4;
        switch (header.colorType) {
            case 0: // greyscale - what the CLI writes a distance field as
                data[target] = data[target + 1] = data[target + 2] = raw[source];
                data[target + 3] = 255;
                break;
            case 2: // RGB
                data[target] = raw[source];
                data[target + 1] = raw[source + 1];
                data[target + 2] = raw[source + 2];
                data[target + 3] = 255;
                break;
            case 3: { // palette
                if (!palette) throw new Error('PNG: a palette image with no PLTE chunk');
                const index = raw[source];
                data[target] = palette[index * 3];
                data[target + 1] = palette[index * 3 + 1];
                data[target + 2] = palette[index * 3 + 2];
                data[target + 3] = transparency && index < transparency.length ? transparency[index] : 255;
                break;
            }
            case 4: // greyscale + alpha
                data[target] = data[target + 1] = data[target + 2] = raw[source];
                data[target + 3] = raw[source + 1];
                break;
            default: // RGBA
                data.set(raw.subarray(source, source + 4), target);
                break;
        }
    }
    return image;
}

function chunk(type: string, body: Uint8Array): Uint8Array {
    const out = new Uint8Array(12 + body.length);
    const view = new DataView(out.buffer);
    view.setUint32(0, body.length);
    for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
    out.set(body, 8);
    view.setUint32(8 + body.length, crc32(out.subarray(4, 8 + body.length)));
    return out;
}

/** A zlib stream of stored (uncompressed) deflate blocks, so no compressor is needed. */
function storedZlib(payload: Uint8Array): Uint8Array {
    const MAX = 65535;
    const blocks = Math.max(1, Math.ceil(payload.length / MAX));
    const out = new Uint8Array(2 + blocks * 5 + payload.length + 4);
    out[0] = 0x78; // CMF: deflate, 32 KB window
    out[1] = 0x01; // FLG: no dictionary - (0x78 << 8 | 0x01) is a multiple of 31, as zlib requires
    let at = 2;
    for (let start = 0; start === 0 || start < payload.length; start += MAX) {
        const slice = payload.subarray(start, Math.min(start + MAX, payload.length));
        out[at++] = start + MAX >= payload.length ? 1 : 0; // BFINAL, then BTYPE = 00 stored
        out[at++] = slice.length & 0xff;
        out[at++] = slice.length >> 8;
        out[at++] = ~slice.length & 0xff;
        out[at++] = (~slice.length >> 8) & 0xff;
        out.set(slice, at);
        at += slice.length;
    }
    const checksum = adler32(payload);
    out[at++] = (checksum >>> 24) & 0xff;
    out[at++] = (checksum >>> 16) & 0xff;
    out[at++] = (checksum >>> 8) & 0xff;
    out[at++] = checksum & 0xff;
    return out.subarray(0, at);
}

/**
 * Encodes 8-bit RGBA, or one greyscale channel when asked - the SDK reads a distance field from
 * red, and the CLI writes those as colour type 0.
 */
export function encodePng(image: RgbaImage, { greyscale = false }: { greyscale?: boolean } = {}): Uint8Array {
    const { width, height, data } = image;
    const channels = greyscale ? 1 : 4;
    const raw = new Uint8Array((width * channels + 1) * height);
    let at = 0;
    for (let y = 0; y < height; y++) {
        raw[at++] = 0; // filter: none. Nothing compresses this, so nothing gains from filtering.
        for (let x = 0; x < width; x++) {
            const source = (y * width + x) * 4;
            if (greyscale) {
                raw[at++] = data[source];
            } else {
                raw[at++] = data[source];
                raw[at++] = data[source + 1];
                raw[at++] = data[source + 2];
                raw[at++] = data[source + 3];
            }
        }
    }

    const ihdr = new Uint8Array(13);
    const view = new DataView(ihdr.buffer);
    view.setUint32(0, width);
    view.setUint32(4, height);
    ihdr[8] = 8;
    ihdr[9] = greyscale ? 0 : 6;
    const header = chunk('IHDR', ihdr);
    const idat = chunk('IDAT', storedZlib(raw));
    const end = chunk('IEND', new Uint8Array(0));

    const out = new Uint8Array(SIGNATURE.length + header.length + idat.length + end.length);
    out.set(SIGNATURE, 0);
    out.set(header, SIGNATURE.length);
    out.set(idat, SIGNATURE.length + header.length);
    out.set(end, SIGNATURE.length + header.length + idat.length);
    return out;
}
