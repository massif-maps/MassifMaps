import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';

import { createImage } from '../dist/mapbox2css/host.js';
import { nodeSpriteHost } from '../dist/mapbox2css/node-host.js';

/*
 * The CLI's own sprite host. The conversion tests all run against the in-memory one, so this is
 * the only place the node implementation is exercised - and it is what a browser host has to
 * behave like.
 */

function gradient(width, height) {
    const image = createImage(width, height);
    for (let i = 0; i < width * height; i++) {
        image.data[i * 4] = i * 7 % 256;
        image.data[i * 4 + 1] = i * 7 % 256;
        image.data[i * 4 + 2] = i * 7 % 256;
        image.data[i * 4 + 3] = 255;
    }
    return image;
}

test('an RGBA image survives encode and decode byte for byte', async () => {
    const source = gradient(6, 4);
    const decoded = await nodeSpriteHost.decodePng(nodeSpriteHost.encodePng(source, {}));
    assert.equal(decoded.width, 6);
    assert.equal(decoded.height, 4);
    assert.deepEqual(Uint8Array.from(decoded.data), source.data);
});

test('greyscale writes one channel and decodes back to r=g=b, opaque', async () => {
    const source = gradient(8, 8);
    const grey = nodeSpriteHost.encodePng(source, { greyscale: true });
    // Lossless for a field, which is the whole reason the option exists - and smaller.
    assert.ok(grey.length < nodeSpriteHost.encodePng(source, {}).length);
    const decoded = await nodeSpriteHost.decodePng(grey);
    assert.deepEqual(Uint8Array.from(decoded.data), source.data);
});

test('writeIcon creates the subdirectory the icon names', () => {
    const dir = mkdtempSync(join(tmpdir(), 'massif-sprite-host-'));
    nodeSpriteHost.writeIcon(dir, 'icons-glyph/poi.png', new Uint8Array([1, 2, 3]));
    assert.deepEqual(readFileSync(join(dir, 'icons-glyph', 'poi.png')), Buffer.from([1, 2, 3]));
});

test('fetchBytes reads a local path as well as a URL', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'massif-sprite-host-'));
    nodeSpriteHost.writeIcon(dir, 'sheet.png', new Uint8Array([9, 8, 7]));
    assert.deepEqual(
        Uint8Array.from(await nodeSpriteHost.fetchBytes(join(dir, 'sheet.png'))),
        new Uint8Array([9, 8, 7]),
    );
});
