/*
 * A road shield outside the US is a flat plate, and the SDK draws one without an image at all.
 * describeFlatPlate is what decides whether a sprite IS one, so these tests are about the line
 * between a plate and artwork - a US interstate has to stay a bitmap.
 */

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { describeFlatPlate } from '../dist/mapbox2css/sprite.js';
import { useMemorySpriteHost } from './sprite-host.js';

useMemorySpriteHost();

const WIDTH = 24;
const HEIGHT = 16;

/**
 * A sheet holding one icon painted by `paint(x, y)`, which returns an [r, g, b, a] or null for
 * nothing at all.
 */
function sheetOf(paint, name = 'plate') {
    const data = Buffer.alloc(WIDTH * HEIGHT * 4);
    for (let y = 0; y < HEIGHT; y++) {
        for (let x = 0; x < WIDTH; x++) {
            const rgba = paint(x, y) ?? [0, 0, 0, 0];
            const i = (y * WIDTH + x) * 4;
            data[i] = rgba[0]; data[i + 1] = rgba[1]; data[i + 2] = rgba[2]; data[i + 3] = rgba[3];
        }
    }
    return new Map([['default', {
        index: { [name]: { x: 0, y: 0, width: WIDTH, height: HEIGHT, pixelRatio: 1 } },
        image: { width: WIDTH, height: HEIGHT, data },
    }]]);
}

const FILL = [200, 16, 46, 255];
const BORDER = [141, 11, 32, 255];

/** A rounded rectangle: `radius` corners, a one-texel border, one flat inside. */
function plate(radius = 3, inside = FILL) {
    return (x, y) => {
        const dx = Math.max(radius - x, x - (WIDTH - 1 - radius), 0);
        const dy = Math.max(radius - y, y - (HEIGHT - 1 - radius), 0);
        if (dx * dx + dy * dy > radius * radius) return null;
        const edge = Math.min(x, y, WIDTH - 1 - x, HEIGHT - 1 - y);
        return edge === 0 || dx * dx + dy * dy > (radius - 1) * (radius - 1) ? BORDER : inside;
    };
}

test('reads a plate\'s own colours off the artwork', () => {
    const found = describeFlatPlate(sheetOf(plate()), 'plate');
    assert.equal(found.fill, '#c8102e');
    assert.equal(found.border, '#8d0b20');
    assert.ok(found.borderWidth >= 1, `border measured as ${found.borderWidth}`);
    assert.ok(found.radius > 1 && found.radius < 5, `radius measured as ${found.radius}`);
});

test('a plate with no border reports its fill as its border', () => {
    const noBorder = (x, y) => (plate()(x, y) === null ? null : FILL);
    const found = describeFlatPlate(sheetOf(noBorder), 'plate');
    assert.equal(found.fill, '#c8102e');
    assert.equal(found.borderWidth, 0);
});

test('a glyph on the plate makes it artwork', () => {
    // Off centre on purpose: the middle texel is where the fill itself is read, so a glyph sitting
    // on it would be taken FOR the fill and the plate would look uniform.
    const withGlyph = (x, y) => {
        const base = plate()(x, y);
        if (base === null) return null;
        return x >= 15 && x < 19 && y >= 6 && y < 10 ? [255, 255, 255, 255] : base;
    };
    assert.equal(describeFlatPlate(sheetOf(withGlyph), 'plate'), null);
});

test('a second field across the top makes it artwork - the interstate crown', () => {
    const crowned = (x, y) => {
        const base = plate()(x, y);
        if (base === null) return null;
        return y < 4 ? [176, 28, 46, 255] : base;
    };
    assert.equal(describeFlatPlate(sheetOf(crowned), 'plate'), null);
});

test('a shape that is not a rectangle keeps its bitmap', () => {
    // A triangle: flat, one colour, and nowhere near filling its box.
    const wedge = (x, y) => (x >= y * 1.5 ? FILL : null);
    assert.equal(describeFlatPlate(sheetOf(wedge), 'plate'), null);
});

test('an SDF sprite is never a plate', () => {
    const sheets = sheetOf(plate());
    sheets.get('default').index.plate.sdf = true;
    assert.equal(describeFlatPlate(sheets, 'plate'), null);
});

test('the border and radius are reported in logical pixels', () => {
    const sheets = sheetOf(plate());
    sheets.get('default').index.plate.pixelRatio = 2;
    const found = describeFlatPlate(sheets, 'plate');
    const atOne = describeFlatPlate(sheetOf(plate()), 'plate');
    assert.equal(found.borderWidth, atOne.borderWidth / 2);
    assert.equal(found.radius, atOne.radius / 2);
});

test('a stroke\'s antialiased outer texel counts toward the border width', () => {
    // A rasteriser spreads a 1.3 px stroke over 2.75 texels, and the outermost falls under the
    // alpha the opaque box is found with - so the walk starts INSIDE the stroke and measured 1.0
    // where the artwork says 1.3. Alpha there is coverage, and is added back.
    const partial = (x, y) => {
        const base = plate()(x, y);
        if (base === null) return null;
        const edge = Math.min(x, y, WIDTH - 1 - x, HEIGHT - 1 - y);
        // A rim at 75% coverage over a solid ring: 1.75 texels of border, of which the walk can
        // only see the solid one, because the rim is under the alpha the box is found with.
        if (edge === 0) return [BORDER[0], BORDER[1], BORDER[2], 191];
        return edge === 1 ? BORDER : base;
    };
    const found = describeFlatPlate(sheetOf(partial), 'plate');
    assert.equal(found.border, '#8d0b20');
    assert.ok(found.borderWidth > 1.5 && found.borderWidth < 2,
        `border measured as ${found.borderWidth}, expected the rim's coverage added to it`);
});
