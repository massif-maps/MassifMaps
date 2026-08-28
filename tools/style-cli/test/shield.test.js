import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { PNG } from 'pngjs';

import { convert } from '../dist/mapbox2css/index.js';
import { isShieldLayer } from '../dist/mapbox2css/shield.js';

/** These tests assert on the translated literals, so they read the style before the palette
  * pass moves them out - see variables.test.js for the hoisting itself. */
const NO_PALETTE = { variables: false };

const TABLE = JSON.parse(readFileSync(new URL('../dist/generated/properties.json', import.meta.url), 'utf8'));

/** The MapTiler shape: the sprite name is built from the ref's length, one image per length. */
const SHIELD_IMAGE = ['concat', 'road_', ['to-string', ['get', 'ref_length']]];

/** A name no `[field]` path can spell - CartoCSS has no `slice` - so the plate is all that is left. */
const PLATE_IMAGE = ['concat', 'road_', ['slice', ['get', 'ref'], 0, 2]];

function symbol(layout, paint) {
    return { id: 'shield', type: 'symbol', 'source-layer': 'road_label', layout, paint };
}

function mss(layer) {
    return convert({ layers: [layer] }, TABLE, NO_PALETTE).mss;
}

test('an unspellable per-feature sprite behind centred text falls back to a plate', () => {
    assert.ok(isShieldLayer(symbol(
        { 'text-field': '{ref}', 'icon-image': PLATE_IMAGE }, { 'icon-color': '#fff' })));
    // A small offset still sits on the icon - MapTiler's bicolor shields use [0, 0.1].
    assert.ok(isShieldLayer(symbol(
        { 'text-field': '{ref}', 'icon-image': PLATE_IMAGE, 'text-offset': [0, 0.1] }, { 'icon-color': '#fff' })));
});

test('a name the fields can spell draws the real artwork, not a plate', () => {
    // mapnik interpolates every [field] in a string, INCLUDING a numeric one, so `road_5.png` is a
    // file the renderer resolves per feature. Faking it with a plate was a misread of a log that
    // only ever names the sprites that FAILED to load.
    assert.ok(!isShieldLayer(symbol(
        { 'text-field': '{ref}', 'icon-image': SHIELD_IMAGE }, { 'icon-color': '#fff' })));

    const index = {};
    for (let n = 1; n <= 3; n++) index[`road_${n}`] = { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: true };
    const sprites = new Map([['default', { index, image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) } }]]);
    const out = convert({ layers: [symbol(
        { 'text-field': '{ref}', 'icon-image': SHIELD_IMAGE }, { 'icon-color': '#fff' })] },
    TABLE, { sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } }).mss;

    assert.match(out, /shield-file: \(url\('icons\/road_\[ref_length\].png'\)\);/);
    assert.ok(!out.includes('text-background-fill'));
});

test('a POI icon is not a shield: its text sits below the icon, not on it', () => {
    assert.ok(!isShieldLayer(symbol(
        { 'text-field': '{name}', 'icon-image': ['get', 'class'], 'text-anchor': 'top', 'text-offset': [0, 0.8] },
        { 'icon-color': '#fff' })));
});

test('a town circle is not a shield: the sprite is picked by ZOOM, not by the feature', () => {
    assert.ok(!isShieldLayer(symbol(
        { 'text-field': '{name}', 'icon-image': ['step', ['zoom'], 'circle', 13, ''] }, { 'icon-color': '#fff' })));
});

test('without an icon-color there is no plate to draw', () => {
    assert.ok(!isShieldLayer(symbol({ 'text-field': '{ref}', 'icon-image': PLATE_IMAGE }, {})));
});

test('the sprite tint becomes the plate and its halo the border', () => {
    const out = mss(symbol(
        { 'text-field': '{ref}', 'icon-image': PLATE_IMAGE, 'text-size': 10 },
        { 'icon-color': '#1a73e8', 'icon-halo-color': '#ffffff', 'icon-halo-width': 1, 'icon-opacity': 0.9 }));
    assert.match(out, /text-background-fill: #1a73e8;/);
    assert.match(out, /text-background-border-fill: #ffffff;/);
    assert.match(out, /text-background-border-width: 1;/);
    assert.match(out, /text-background-opacity: 0.9;/);
    assert.match(out, /text-background-radius: 2;/);
    assert.ok(!out.includes('marker-file'));
});

test('a text-field branching per country keeps its fallback rather than losing the label', () => {
    // MapBox's own shields slice the ref for BR; CartoCSS has no slice, and the fallback is [ref].
    const brazil = ['case', ['==', ['get', 'iso_a2'], 'BR'], ['slice', ['get', 'ref'], 3], ['get', 'ref']];
    const { mss: out, coverage } = convert({ layers: [symbol(
        { 'text-field': brazil, 'icon-image': PLATE_IMAGE }, { 'icon-color': '#1a73e8' })] }, TABLE, NO_PALETTE);
    assert.match(out, /text-name: \[ref\];/);
    assert.match(out, /text-background-fill: #1a73e8;/);
    assert.ok(coverage.report().includes('kept only its fallback branch'));
});

test('an icon beside text becomes ONE shield, not a marker that culls the label', () => {
    const sprites = new Map([['default', {
        index: { circle: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: true } },
        image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) },
    }]]);
    const out = convert({ layers: [symbol(
        { 'text-field': '{name}', 'icon-image': 'circle', 'text-anchor': 'bottom', 'text-size': 12 },
        { 'icon-color': '#000000', 'text-color': '#333333' })] },
    TABLE, { ...NO_PALETTE, sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } }).mss;

    assert.ok(!out.includes('marker-'), 'a marker beside the text would be a second, colliding label');
    // The field is kept and tinted by the style, not resolved to pixels here.
    assert.match(out, /shield-file: url\('icons\/circle.png'\);/);
    assert.match(out, /shield-sdf: true;/);
    assert.match(out, /shield-icon-fill: #000000;/);
    assert.match(out, /shield-name: \[name\];/);
    assert.match(out, /shield-fill: #333333;/);
    assert.match(out, /shield-unlock-image: true;/);
    // 'bottom' anchors the text's bottom edge, so the text is above and the icon drops below it -
    // by half the bitmap, which an SDF icon carries SDF_PADDING of field around (8 + 2*6 = 20).
    assert.match(out, /shield-dy: 10;/);
});

test('an SDF icon carries field around it, so the halo has room to fade before the quad ends', () => {
    // MapBox's field only describes ~2 texels outside the ink and a sprite cell is cut tight, so
    // the renderer drew the halo along the QUAD BORDER: a white rectangle round every POI icon.
    const sprites = new Map([['default', {
        index: { circle: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: true } },
        image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) },
    }]]);
    convert({ layers: [symbol({ 'text-field': '{name}', 'icon-image': 'circle' }, { 'icon-color': '#000' })] },
        TABLE, { sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } });

    const png = PNG.sync.read(readFileSync('/tmp/massif-style-test/icons/circle.png'));
    assert.equal(png.width, 20, 'padded on both sides');
    assert.equal(png.height, 20);
    // The corner is the furthest from the ink, so it is the closest to "fully outside" (0).
    assert.ok(png.data[0] < png.data[(10 * 20 + 10) * 4], 'the field falls off towards the border');
});

/**
 * A MapBox vector icon's flat render: a square with a one-texel ring, a disc under it and a block
 * of ink in the middle - the three flats extractIconPlate has to tell apart.
 */
function compositeSheet(name = 'poi') {
    const side = 16;
    const data = Buffer.alloc(side * side * 4);
    const put = (x, y, [r, g, b, a]) => {
        const i = (y * side + x) * 4;
        data[i] = r; data[i + 1] = g; data[i + 2] = b; data[i + 3] = a;
    };
    for (let y = 0; y < side; y++) {
        for (let x = 0; x < side; x++) {
            const edge = Math.min(x, y, side - 1 - x, side - 1 - y);
            if (edge === 0) put(x, y, [0, 0, 0, 0]);            // transparent surround
            else if (edge === 1) put(x, y, [200, 200, 200, 255]); // the ring
            else if (x >= 6 && x < 10 && y >= 6 && y < 10) put(x, y, [255, 255, 255, 255]); // the glyph
            else put(x, y, [100, 150, 220, 255]);                // the disc
        }
    }
    return new Map([['default', {
        index: { [name]: { x: 0, y: 0, width: side, height: side, pixelRatio: 1 } },
        image: { width: side, height: side, data },
    }]]);
}

test('a recolourable icon is split into a glyph field and the disc it sat on', () => {
    // MapBox colours the disc, its ring and the glyph per feature through `["image", …, {params}]`,
    // and the sheet ships ONE flat render with the icon's own defaults. Split, none of the three is
    // baked: the glyph becomes a field and the disc the shield's plate, both coloured by the style.
    const out = convert({ layers: [symbol(
        {
            'text-field': '{name}',
            'icon-image': ['image', ['get', 'maki'], { params: {
                background: '#ff0000', 'background-stroke': '#00ff00', icon: '#0000ff',
            } }],
        },
        {})] },
    TABLE, { ...NO_PALETTE, sprites: { sheets: compositeSheet(), outDir: '/tmp/massif-style-test' } }).mss;

    assert.match(out, /shield-sdf: true;/);
    assert.match(out, /shield-icon-fill: #0000ff;/, 'the glyph takes the icon param');
    assert.match(out, /shield-icon-background-fill: #ff0000;/, 'the disc is the plate');
    assert.match(out, /shield-icon-background-border-fill: #00ff00;/);
    assert.match(out, /shield-icon-background-border-width: 1;/, 'the ring the field was cropped by');
    // The field is cropped to the disc, so the plate sits exactly on it.
    assert.match(out, /shield-icon-background-padding-x: 0;/);
    assert.match(out, /shield-icon-background-padding-y: 0;/);
    // A square disc, so no corner rounding to reproduce.
    assert.match(out, /shield-icon-background-radius: 0;/);

    // NOTHING is baked into the file: a preset overrides the colours above and reuses this one.
    const png = PNG.sync.read(readFileSync('/tmp/massif-style-test/icons-glyph/poi.png'));
    assert.equal(png.width, 12, 'cropped to the disc, ring and surround off');
    for (let i = 0; i < png.data.length; i += 4) {
        assert.equal(png.data[i], png.data[i + 1]);
        assert.equal(png.data[i + 1], png.data[i + 2]);
        assert.equal(png.data[i + 3], 255);
    }
    const centre = png.data[((6 * 12) + 6) * 4];
    const corner = png.data[0];
    assert.ok(centre > 127.5 && corner < 127.5, 'inside the glyph, outside at the corner');
});

test('a recolourable sprite that is not a field is drawn as artwork, never as one', () => {
    // `shield-sdf` makes the renderer read RED as signed distance. Standard's transit roundel is a
    // blue disc with a white glyph and no `sdf` flag: taken as a field, the glyph read as the
    // inside and the disc as the outside, so it drew inverted and lost its background.
    const sprites = new Map([['default', {
        index: { bus: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1 } },
        image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) },
    }]]);
    const { mss: out, coverage } = convert({ layers: [symbol(
        { 'text-field': '{name}', 'icon-image': ['image', 'bus', { params: { background: '#f00' } }] },
        {})] }, TABLE, { ...NO_PALETTE, sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } });

    assert.match(out, /shield-file: url\('icons\/bus.png'\);/);
    assert.ok(!out.includes('shield-sdf'), 'a colour bitmap is not a distance field');
    assert.ok(!out.includes('shield-icon-halo'), 'the ring is in the artwork, not grown from a field');
    assert.ok(coverage.report().includes('recolours its icon per feature'));
});

test('only a STATED occlusion opacity is carried, and a label carries one', () => {
    // MapBox's absent value is "occluded by the terrain alone", which is what the SDK does anyway;
    // a stated one adds 3D content. Standard sets it on its road and water names and says nothing
    // on poi-label, so translating only what is stated is what keeps POI labels drawn.
    const plain = mss(symbol({ 'text-field': '{name}' }, { 'text-occlusion-opacity': 0 }));
    assert.match(plain, /text-occlusion-opacity: 0;/);
    assert.ok(!mss(symbol({ 'text-field': '{name}' }, {})).includes('occlusion-opacity'));

    // The SDK's is a TextSymbolizer property, so it is renamed with the rest of the shield.
    const sprites = new Map([['default', {
        index: { circle: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: true } },
        image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) },
    }]]);
    const shielded = convert({ layers: [symbol(
        { 'text-field': '{name}', 'icon-image': 'circle' },
        { 'icon-color': '#000', 'text-occlusion-opacity': 0.1 })] },
    TABLE, { ...NO_PALETTE, sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } }).mss;
    assert.match(shielded, /shield-occlusion-opacity: 0.1;/);

    // One label, one value: the icon's is taken only where the text states none.
    assert.match(mss(symbol({ 'text-field': '{name}' }, { 'icon-occlusion-opacity': 0 })),
        /text-occlusion-opacity: 0;/);
    const both = convert({ layers: [symbol({ 'text-field': '{name}' },
        { 'text-occlusion-opacity': 0, 'icon-occlusion-opacity': 0.5 })] }, TABLE, NO_PALETTE);
    assert.ok(!both.mss.includes('0.5'), 'the text value wins, and the icon one is reported');
    assert.ok(both.coverage.report().includes('the label carries one occlusion opacity'));

    // A marker is not a label, so there is nothing to hang it on.
    const iconOnly = convert({ layers: [symbol({ 'icon-image': 'circle' }, { 'icon-occlusion-opacity': 0 })] },
        TABLE, { ...NO_PALETTE, sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } });
    assert.ok(!iconOnly.mss.includes('occlusion-opacity'));
    assert.ok(iconOnly.coverage.report().includes('a marker, which is not a label'));
});

test('an icon-overlap alone never builds a fileless marker', () => {
    // marker-allow-overlap on its own makes a MarkersSymbolizer with no file, whose default fill is
    // #0000ff - a blue ellipse over every airport whose sprite could not be resolved.
    const { mss: out } = convert({ layers: [symbol(
        { 'text-field': '{name}', 'icon-image': ['get', 'subclass'], 'icon-overlap': 'never',
            'text-anchor': 'top', 'text-offset': [0, 1] }, { 'icon-color': '#333' })] }, TABLE, NO_PALETTE);
    assert.ok(!out.includes('marker-'));
    assert.match(out, /text-name: \[name\];/);
});

test('a shield whose text cannot be translated draws nothing, not an empty box', () => {
    const { mss: out, coverage } = convert({ layers: [symbol(
        { 'text-field': ['slice', ['get', 'ref'], 3], 'icon-image': PLATE_IMAGE }, { 'icon-color': '#1a73e8' })] }, TABLE, NO_PALETTE);
    assert.ok(!out.includes('text-background-fill'));
    assert.ok(coverage.report().includes('no usable text-field'));
});

test('a variable anchor becomes the shield anchor list, with its gap and its fallback', () => {
    // MapBox tries each side until the label fits and keeps the icon alone when none does.
    // ShieldSymbolizer takes the same list, so the four properties describing it map straight over.
    const index = { pin: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: true } };
    const sprites = new Map([['default', { index, image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) } }]]);
    const { mss: out, coverage } = convert({ layers: [symbol({
        'text-field': '{name}', 'icon-image': 'pin', 'text-size': 12,
        'text-variable-anchor': ['right', 'left', 'top', 'bottom'],
        'text-optional': true, 'text-radial-offset': 0.5, 'text-justify': 'auto',
    })] }, TABLE, { ...NO_PALETTE, sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } });

    assert.match(out, /shield-anchors: 'right,left,top,bottom';/);
    assert.match(out, /shield-text-optional: true;/);
    // The gap is stated once as dx - the SDK mirrors it onto whichever side wins - in pixels of
    // the text size, not ems.
    assert.match(out, /shield-text-dx: 6;/);
    assert.match(out, /shield-text-horizontal-alignment: 'auto';/);
    assert.ok(!/text-justify|text-optional +[^:]/.test(coverage.report()));
});

test('a corner anchor loses its hyphen, which is how the SDK spells it', () => {
    const index = { pin: { x: 0, y: 0, width: 8, height: 8, pixelRatio: 1, sdf: true } };
    const sprites = new Map([['default', { index, image: { width: 8, height: 8, data: Buffer.alloc(8 * 8 * 4, 200) } }]]);
    const out = convert({ layers: [symbol({
        'text-field': '{name}', 'icon-image': 'pin',
        'text-variable-anchor': ['top-left', 'bottom-right'],
    })] }, TABLE, { ...NO_PALETTE, sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } }).mss;
    assert.match(out, /shield-anchors: 'topleft,bottomright';/);
});

test('an anchor property on a layer with no icon says so, rather than reading as unmapped', () => {
    const { coverage } = convert({ layers: [symbol({ 'text-field': '{name}', 'text-justify': 'center' })] }, TABLE, NO_PALETTE);
    assert.ok(coverage.report().includes('positions the text against an icon'));
});

test('an icon-size of 1 over a 2x sheet is half, not zero', () => {
    // `/` between two INTEGERS truncates in the decoder, so `((1) / 2)` came out 0 and every icon
    // of thirteen POI layers drew at zero size. The divisor is written as a float for that reason.
    const index = { pin: { x: 0, y: 0, width: 16, height: 16, pixelRatio: 2, sdf: true } };
    const sprites = new Map([['default', { index, image: { width: 16, height: 16, data: Buffer.alloc(16 * 16 * 4, 200) } }]]);
    const out = convert({ layers: [symbol({ 'text-field': '{name}', 'icon-image': 'pin' })] },
        TABLE, { sprites: { sheets: sprites, outDir: '/tmp/massif-style-test' } }).mss;
    assert.match(out, /shield-image-scale: \(\(1\) \/ 2\.0\);/);
});
