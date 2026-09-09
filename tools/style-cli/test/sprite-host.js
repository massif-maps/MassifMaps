/**
 * A sprite host that keeps the written icons in memory.
 *
 * Encoding is the real one, so a test that decodes an icon back still checks the pixels the CLI
 * would have written - it just does not go through /tmp, which every test file used to share under
 * the same name.
 */

import { nodeSpriteHost } from '../dist/mapbox2css/node-host.js';
import { setSpriteHost } from '../dist/mapbox2css/sprite.js';

const written = new Map();

export const memorySpriteHost = {
    async fetchBytes(url) {
        throw new Error(`memorySpriteHost fetches nothing (asked for ${url})`);
    },
    async decodePng(bytes) {
        return nodeSpriteHost.decodePng(bytes);
    },
    encodePng(image, encoding) {
        return nodeSpriteHost.encodePng(image, encoding);
    },
    writeIcon(outDir, file, bytes) {
        written.set(file, bytes);
    },
};

/** Installs the host and forgets anything written so far. */
export function useMemorySpriteHost() {
    written.clear();
    setSpriteHost(memorySpriteHost);
}

/** The bytes written at `file` - a path relative to the project, e.g. `icons/circle.png`. */
export function readIcon(file) {
    const bytes = written.get(file);
    if (!bytes) throw new Error(`No icon written at ${file}; got ${[...written.keys()].join(', ')}`);
    return Buffer.from(bytes);
}

export function writtenIcons() {
    return [...written.keys()];
}
