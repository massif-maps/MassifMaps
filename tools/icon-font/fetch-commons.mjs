#!/usr/bin/env node
/**
 * Downloads the `commons` entries of sources.json into local/, under the SPRITE name, and records
 * what each one is in local/SOURCES.json - file, licence, author, and the description page a
 * reviewer has to be able to reach.
 *
 * Refuses anything the API does not report as public domain or CC0. These are live trademarks and
 * the copyright tag is the only part this can check; the rest is a judgement, not a query.
 *
 * A downloaded file is NOT a glyph yet: it is the whole badge, in colour. Stripping it to its
 * content is by hand - see README.md.
 */
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const LOCAL = join(HERE, 'local');
const UA = 'massif-maps-icon-font/1.0 (https://github.com/massif-maps/MassifMaps)';
/** Anything else is a judgement call, and this is not the place to make it silently. */
const FREE = /public domain|^cc0/i;

const commons = Object.fromEntries(
    Object.entries(JSON.parse(readFileSync(join(HERE, 'sources.json'), 'utf8')).commons ?? {})
        .filter(([k]) => !k.startsWith('$')));

const titles = Object.values(commons).map((t) => `File:${t}`).join('|');
const api = new URL('https://commons.wikimedia.org/w/api.php');
api.search = new URLSearchParams({
    action: 'query', format: 'json', prop: 'imageinfo', titles,
    iiprop: 'url|size|extmetadata', iiextmetadatafilter: 'LicenseShortName|Artist|LicenseUrl',
}).toString();

const info = await fetch(api, { headers: { 'User-Agent': UA } }).then((r) => r.json());
const pages = Object.fromEntries(
    Object.values(info.query.pages).map((p) => [p.title, p]));

mkdirSync(LOCAL, { recursive: true });
const record = {};
const skipped = [];
for (const [sprite, title] of Object.entries(commons)) {
    const page = pages[`File:${title}`];
    if (!page || page.missing !== undefined) {
        skipped.push(`${sprite}: File:${title} is not on Commons`);
        continue;
    }
    const [image] = page.imageinfo;
    const meta = image.extmetadata ?? {};
    const licence = meta.LicenseShortName?.value ?? 'unknown';
    if (!FREE.test(licence)) {
        skipped.push(`${sprite}: "${licence}" is not public domain or CC0 - check it by hand`);
        continue;
    }
    // The url the API hands back carries its own analytics query; the file is the path.
    const source = image.url.split('?')[0];
    const response = await fetch(source, { headers: { 'User-Agent': UA } });
    const svg = await response.text();
    // upload.wikimedia.org answers an error with a 2 kB HTML page, and three of these landed as
    // one - same length, same MediaWiki link blue, no shape at all. Check what came back rather
    // than trusting the status alone: the SIZE the API reported is the thing to match.
    if (!response.ok || !/^\s*(<\?xml|<svg)/i.test(svg)) {
        skipped.push(`${sprite}: ${response.status} from ${source}, and the body is not an SVG`);
        continue;
    }
    if (Buffer.byteLength(svg) !== image.size) {
        skipped.push(`${sprite}: got ${Buffer.byteLength(svg)} B where Commons reports ${image.size}`);
        continue;
    }
    // Nothing here executes an SVG, but a script in one has no business in a glyph either.
    if (/<script[\s>]/i.test(svg)) {
        skipped.push(`${sprite}: File:${title} carries a <script> element - not downloaded`);
        continue;
    }
    writeFileSync(join(LOCAL, `${sprite}.svg`), svg);
    record[sprite] = {
        file: `File:${title}`,
        page: `https://commons.wikimedia.org/wiki/${encodeURIComponent(`File:${title}`)}`,
        source,
        licence,
        author: (meta.Artist?.value ?? '').replace(/<[^>]*>/g, '').trim() || 'unstated',
        bytes: image.size,
    };
    process.stdout.write(`  ${sprite.padEnd(24)} ${licence.padEnd(16)} ${title}\n`);
}

writeFileSync(join(LOCAL, 'SOURCES.json'), `${JSON.stringify({
    $comment: 'Written by fetch-commons.mjs. Public domain for COPYRIGHT; the marks are still '
        + 'registered trademarks of their operators.',
    icons: record,
}, null, 2)}\n`);

process.stdout.write(`\n${Object.keys(record).length} downloaded into local/\n`);
if (skipped.length) process.stdout.write(`skipped:\n  ${skipped.join('\n  ')}\n`);
if (!existsSync(join(LOCAL, 'SOURCES.json'))) process.exitCode = 1;
