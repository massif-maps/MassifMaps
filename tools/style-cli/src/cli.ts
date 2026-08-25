#!/usr/bin/env node
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { convert } from './mapbox2css/index.js';
import { loadSprites } from './mapbox2css/sprite.js';
import type { MapboxStyle, PropertyTable } from './mapbox2css/types.js';
import { WasmMissing, runWasm, wasmAvailable } from './wasm.js';

const HERE = dirname(fileURLToPath(import.meta.url));

const USAGE = `Usage: massif-style <command> [options] [args]

  mapbox2css <style.json> <out-dir> [--validate] [--strict] [--no-sprite]
                                    [--sprite-key '?key=...']
                                    [--contour-schema div] [--contour-major-div N]
      translate a MapBox/MapLibre style to a CartoCSS project

      --sprite-key          query string appended to the style's sprite URLs, for a
                            provider that needs a key
      --no-sprite           skip the sprite; every icon-image is then dropped
      --sdf-flatten         resolve SDF icons to plain bitmaps, for an SDK without
                            marker-sdf; loses the zoom-driven size and the halo
      --contour-schema div  rewrite contour-layer nth_line tests onto a div (interval in
                            metres) attribute; --contour-major-div is the major threshold,
                            default 100

  css2xml [--roundtrip] <project.json> <out.xml>
      compile a CartoCSS style project to mapnik XML
`;

function loadPropertyTable(): PropertyTable {
    // dist/cli.js -> dist/generated; src/cli.ts -> src/generated.
    return JSON.parse(readFileSync(join(HERE, 'generated', 'properties.json'), 'utf8')) as PropertyTable;
}

/** `--flag`, `--key value` and `--key=value`, which is as much as this CLI needs. */
function parseFlags(args: string[]): { flags: Map<string, string>; positional: string[] } {
    const flags = new Map<string, string>();
    const positional: string[] = [];
    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        if (!arg.startsWith('--')) {
            positional.push(arg);
            continue;
        }
        const eq = arg.indexOf('=');
        if (eq !== -1) {
            flags.set(arg.slice(2, eq), arg.slice(eq + 1));
        } else if (i + 1 < args.length && !args[i + 1].startsWith('--') && VALUE_FLAGS.has(arg.slice(2))) {
            flags.set(arg.slice(2), args[++i]);
        } else {
            flags.set(arg.slice(2), '');
        }
    }
    return { flags, positional };
}

const VALUE_FLAGS = new Set(['contour-schema', 'contour-major-div', 'sprite-key']);

async function mapbox2css(args: string[]): Promise<number> {
    const { flags, positional } = parseFlags(args);
    const [input, outDir] = positional;
    if (!input || !outDir) {
        process.stderr.write(USAGE);
        return 2;
    }

    const contourSchema = flags.get('contour-schema');
    if (contourSchema !== undefined && contourSchema !== 'div') {
        process.stderr.write(`Unknown --contour-schema "${contourSchema}"; only "div" is supported.\n`);
        return 2;
    }

    const style = JSON.parse(readFileSync(input, 'utf8')) as MapboxStyle;

    // Icons need the sprite sheet, which the style only points at - so this is opt-in and says
    // what it fetches rather than reaching out silently.
    let sprites;
    if (!flags.has('no-sprite')) {
        try {
            const sheets = await loadSprites(style, flags.get('sprite-key') ?? '');
            if (sheets.size > 0) {
                process.stdout.write(`Loaded ${sheets.size} sprite sheet(s).\n`);
                sprites = { sheets, outDir };
            }
        } catch (error) {
            process.stderr.write(
                `Sprite not loaded (${error instanceof Error ? error.message : String(error)}); icons will be dropped.\n`,
            );
        }
    }

    const { mss, project, coverage } = convert(style, loadPropertyTable(), {
        sprites,
        flattenSdf: flags.has('sdf-flatten'),
        contour: contourSchema === 'div'
            ? { schema: 'div', majorDiv: Number(flags.get('contour-major-div') ?? 100) }
            : undefined,
    });

    mkdirSync(outDir, { recursive: true });
    writeFileSync(join(outDir, 'style.mss'), mss);
    writeFileSync(join(outDir, 'project.json'), project);
    process.stdout.write(`${coverage.report()}\n`);

    if (flags.has('validate')) {
        if (!wasmAvailable()) {
            process.stderr.write('--validate needs massif-style.mjs; skipping validation.\n');
            return 1;
        }
        const status = await runWasm(['css2xml', join(outDir, 'project.json'), join(outDir, 'style.xml')]);
        if (status !== 0) {
            process.stderr.write('Validation failed: the generated CartoCSS did not compile.\n');
            return status;
        }
        process.stdout.write('Validated: the generated CartoCSS compiles.\n');
    }

    if (flags.has('strict') && coverage.droppedCount > 0) {
        process.stderr.write(`--strict: ${coverage.droppedCount} properties dropped.\n`);
        return 1;
    }
    return 0;
}

async function main(argv: string[]): Promise<number> {
    const [command, ...args] = argv;
    if (!command || command === '--help' || command === '-h' || command === 'help') {
        process.stdout.write(USAGE);
        return command ? 0 : 2;
    }

    try {
        if (command === 'mapbox2css') return await mapbox2css(args);
        if (command === 'css2xml') return await runWasm([command, ...args]);
    } catch (error) {
        if (error instanceof WasmMissing) {
            process.stderr.write(`${error.message}\n`);
            return 1;
        }
        throw error;
    }

    process.stderr.write(`Unknown command: ${command}\n\n${USAGE}`);
    return 2;
}

main(process.argv.slice(2)).then(
    (code) => { process.exitCode = code; },
    (error: unknown) => {
        process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`);
        process.exitCode = 1;
    },
);
