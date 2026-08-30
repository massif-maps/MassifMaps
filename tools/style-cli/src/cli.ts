#!/usr/bin/env node
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { VARIABLES_FILE, convert } from './mapbox2css/index.js';
import { loadSprites } from './mapbox2css/sprite.js';
import type { Json, MapboxStyle, PropertyTable } from './mapbox2css/types.js';
import { WasmMissing, runWasm, wasmAvailable } from './wasm.js';

const HERE = dirname(fileURLToPath(import.meta.url));

const USAGE = `Usage: massif-style <command> [options] [args]

  mapbox2css <style.json> <out-dir> [--validate] [--strict] [--no-sprite]
                                    [--sprite-key '?key=...']
                                    [--contour-schema div] [--contour-major-div N]
                                    [--label-spacing N] [--schema openmaptiles]
                                    [--source-schema mapbox|maptiler]
      translate a MapBox/MapLibre style to a CartoCSS project

      --sprite-key          query string appended to the style's sprite URLs, for a
                            provider that needs a key
      --label-spacing N     multiply the collision gap between labels; 1 is what the
                            style asks for, higher thins the map out
      --schema NAME         retarget the style's source layers at another tile schema.
                            'openmaptiles' is the only target. A MapTiler planet_v4 style
                            becomes the OpenMapTiles layer plus the class filter that
                            stands in for its per-feature-type split (forest, grass,
                            city_label, peak, ...); a MapBox Streets v8 style is further
                            away and also gets its field names (height ->
                            render_height), its field values (class 'street' -> 'minor')
                            and its one-layer-two-targets splits (road is both
                            transportation and transportation_name) rewritten. A layer
                            with no equivalent is dropped and named in the coverage report
      --source-schema NAME  which vocabulary the style itself is written in, 'mapbox' or
                            'maptiler'. Detected from the source layer names when unset,
                            and conversion stops rather than guess when they are ambiguous
      --no-sprite           skip the sprite; every icon-image is then dropped
      --no-variables        keep the colours, fonts and sizes inline in style.mss instead of
                            hoisting them into a variables.mss a variant can override
      --no-presets          do not emit the extra lightPreset palettes (Mapbox Standard)
      --config k=v          set one of the style's own config values, for a style that declares a
                            schema (Mapbox Standard). Repeatable, and k=v,k2=v2 also works. Every
                            config read is resolved to a constant, so this is a build-time choice:
                            --config lightPreset=night is how the dark version is produced
      --live-light          leave the 2D colours as the style authored them and carry its
                            emissive strengths instead, for an SDK that lights them at draw
                            time. One palette then covers every light preset and the hour can
                            be changed at runtime. Off, the light is folded into the colours
                            and a palette is emitted per preset
      --label-emissive N    a CAP on every text and icon emissive, and the default where the
                            style states none. MapBox's own default is 1 - a label drawn as
                            authored at any hour - and Standard states 1 on its shields
                            deliberately, so a cap is what makes them follow the light too.
                            Needs --live-light
      --halo-emissive N     the same for a label's HALO, which the SDK grades separately: the
                            ink keeps its emissive and stays bright while the outline takes
                            this one and goes dark with the scene, so a name stays readable
                            over a map that darkens. Needs --live-light
      --geometry-emissive N a FLOOR on what a fill, line or background emissive defaults to
                            where the style states none. MapBox's own default is 0 -
                            entirely at the mercy of the light - which collapses a style
                            with no light model of its own as soon as the sun is down.
                            Needs --live-light
      --building-height-ramp  grow the extrusions in over a third of a zoom level where the
                            style states no fill-extrusion-vertical-scale of its own
      --sdf-flatten         resolve SDF icons to plain bitmaps, for an SDK without
                            marker-sdf; loses the zoom-driven size and the halo
      --contour-schema div  rewrite contour-layer nth_line tests onto a div (interval in
                            metres) attribute; --contour-major-div is the major threshold,
                            and --contour-elevation is what the target tiles call the
                            elevation (default ele; MapTiler's own say height),
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

const VALUE_FLAGS = new Set(['contour-schema', 'contour-major-div', 'sprite-key', 'label-spacing', 'label-emissive', 'halo-emissive', 'geometry-emissive', 'contour-elevation', 'schema', 'source-schema', 'config']);

/**
 * `--config key=value`, repeatable, for a style with a `schema` (Mapbox Standard). Values are read
 * as JSON when they parse - so `show3dObjects=false` and `densityPointOfInterestLabels=2` arrive as
 * a boolean and a number - and as a plain string otherwise, which is what a colour or a preset is.
 */
function parseConfig(args: string[]): Record<string, Json> {
    const config: Record<string, Json> = {};
    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        const pair = arg === '--config' ? args[++i] : arg.startsWith('--config=') ? arg.slice('--config='.length) : null;
        if (!pair) continue;
        for (const entry of pair.split(',')) {
            const eq = entry.indexOf('=');
            if (eq === -1) continue;
            const [key, raw] = [entry.slice(0, eq).trim(), entry.slice(eq + 1).trim()];
            try {
                config[key] = JSON.parse(raw) as Json;
            } catch {
                config[key] = raw;
            }
        }
    }
    return config;
}

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

    const schema = flags.get('schema');
    if (schema !== undefined && schema !== 'openmaptiles') {
        process.stderr.write(`Unknown --schema "${schema}"; only "openmaptiles" is supported.\n`);
        return 2;
    }

    const sourceSchema = flags.get('source-schema');
    if (sourceSchema !== undefined && sourceSchema !== 'mapbox' && sourceSchema !== 'maptiler') {
        process.stderr.write(`Unknown --source-schema "${sourceSchema}"; "mapbox" or "maptiler".\n`);
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

    const { mss, project, coverage, variables, presets, defaultPreset, presetOverrides } = convert(style, loadPropertyTable(), {
        sprites,
        variables: !flags.has('no-variables'),
        liveLight: flags.has('live-light'),
        labelEmissive: flags.has('label-emissive') ? Number(flags.get('label-emissive')) : undefined,
        haloEmissive: flags.has('halo-emissive') ? Number(flags.get('halo-emissive')) : undefined,
        geometryEmissive: flags.has('geometry-emissive') ? Number(flags.get('geometry-emissive')) : undefined,
        buildingHeightRamp: flags.has('building-height-ramp'),
        config: parseConfig(args),
        presets: flags.has('no-presets') ? [] : undefined,
        flattenSdf: flags.has('sdf-flatten'),
        labelSpacing: Number(flags.get('label-spacing') ?? 1),
        schema: schema === 'openmaptiles' ? 'openmaptiles' : undefined,
        sourceSchema,
        contour: contourSchema === 'div'
            ? {
                schema: 'div',
                majorDiv: Number(flags.get('contour-major-div') ?? 100),
                elevationField: flags.get('contour-elevation') ?? undefined,
            }
            : undefined,
    });

    mkdirSync(outDir, { recursive: true });
    writeFileSync(join(outDir, 'style.mss'), mss);
    writeFileSync(join(outDir, 'project.json'), project);
    if (variables) writeFileSync(join(outDir, VARIABLES_FILE), variables);
    // One palette per other light preset, plus the project that picks it. Same style.mss.
    const overrides = presetOverrides ?? new Map<string, Record<string, unknown>>();
    for (const [preset, palette] of presets) {
        writeFileSync(join(outDir, `${preset}.mss`), palette);
        const params = overrides.get(preset);
        writeFileSync(join(outDir, `${preset}.json`), `${JSON.stringify({
            extends: './project.json', styles: [`${preset}.mss`, 'style.mss'],
            ...(params ? { styleparameters: params } : {}),
        }, null, 2)}\n`);
    }
    if (defaultPreset) {
        writeFileSync(join(outDir, `${defaultPreset}.json`), `${JSON.stringify({
            extends: './project.json', styles: [VARIABLES_FILE, 'style.mss'],
        }, null, 2)}\n`);
    }
    if (presets.size > 0) {
        process.stdout.write(`Light presets: ${[defaultPreset, ...presets.keys()].filter(Boolean).join(', ')}`
            + ' (one palette each, over the same style.mss).\n');
    }
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
