import assert from 'node:assert/strict';
import { test } from 'node:test';

import { PNG } from 'pngjs';

import { createImage } from '../dist/mapbox2css/host.js';
import { decodePng, encodePng } from '../dist/mapbox2css/png.js';

/*
 * The browser codec. It has to agree with pngjs BYTE FOR BYTE, because the same converter runs
 * through either one and the SDK reads a sprite's alpha as a distance field - a value that is
 * "close enough" for a photo is a mangled icon here.
 */

function noisy(width, height) {
    const image = createImage(width, height);
    for (let i = 0; i < width * height * 4; i++) {
        // Deliberately includes partly transparent texels: they are what a premultiplying canvas
        // gets wrong, and what a sprite sheet is mostly made of.
        image.data[i] = (i * 37 + (i % 5) * 61) % 256;
    }
    return image;
}

test('a round trip through the browser codec changes nothing', async () => {
    const source = noisy(17, 9);
    const decoded = await decodePng(encodePng(source, {}));
    assert.equal(decoded.width, 17);
    assert.equal(decoded.height, 9);
    assert.deepEqual(decoded.data, source.data);
});

test('what the browser writes, pngjs reads back identically', () => {
    const source = noisy(13, 11);
    const read = PNG.sync.read(Buffer.from(encodePng(source, {})));
    assert.equal(read.width, 13);
    assert.deepEqual(Uint8Array.from(read.data), source.data);
});

test('what pngjs writes, the browser reads back identically', async () => {
    const source = noisy(12, 12);
    const png = new PNG({ width: 12, height: 12 });
    png.data = Buffer.from(source.data);
    // deflateLevel/filterType are the CLI's own, so this is the exact byte layout it produces.
    const decoded = await decodePng(PNG.sync.write(png, { deflateLevel: 9, filterType: -1 }));
    assert.deepEqual(decoded.data, source.data);
});

test('a greyscale field survives both ways - it is what an SDF icon is stored as', async () => {
    const source = createImage(9, 5);
    for (let i = 0; i < 9 * 5; i++) {
        const value = (i * 11) % 256;
        source.data[i * 4] = source.data[i * 4 + 1] = source.data[i * 4 + 2] = value;
        source.data[i * 4 + 3] = 255;
    }
    assert.deepEqual((await decodePng(encodePng(source, { greyscale: true }))).data, source.data);
    // And the CLI's greyscale output reads the same way.
    const png = new PNG({ width: 9, height: 5 });
    png.data = Buffer.from(source.data);
    const fromCli = PNG.sync.write(png, { colorType: 0, deflateLevel: 9, filterType: -1 });
    assert.deepEqual((await decodePng(fromCli)).data, source.data);
});

test('a sheet wider than one stored block still round-trips', async () => {
    // 65535 bytes is the largest stored deflate block, so this crosses the boundary.
    const source = noisy(200, 100);
    assert.ok((200 * 4 + 1) * 100 > 65535);
    assert.deepEqual((await decodePng(encodePng(source, {}))).data, source.data);
});

test('a corrupt file is refused rather than decoded into noise', async () => {
    await assert.rejects(() => decodePng(new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8])), /not a PNG/);
});
