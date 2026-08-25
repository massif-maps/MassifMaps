#!/usr/bin/env node
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { convert } from './mapbox2css/index.js';
import type { MapboxStyle, PropertyTable } from './mapbox2css/types.js';
import { WasmMissing, runWasm, wasmAvailable } from './wasm.js';

const HERE = dirname(fileURLToPath(import.meta.url));

const USAGE = `Usage: massif-style <command> [options] [args]

  mapbox2css <style.json> <out-dir> [--validate] [--strict]
      translate a MapBox/MapLibre style to a CartoCSS project

  css2xml [--roundtrip] <project.json> <out.xml>
      compile a CartoCSS style project to mapnik XML
`;

function loadPropertyTable(): PropertyTable {
    // dist/cli.js -> dist/generated; src/cli.ts -> src/generated.
    return JSON.parse(readFileSync(join(HERE, 'generated', 'properties.json'), 'utf8')) as PropertyTable;
}

async function mapbox2css(args: string[]): Promise<number> {
    const flags = new Set(args.filter((a) => a.startsWith('--')));
    const [input, outDir] = args.filter((a) => !a.startsWith('--'));
    if (!input || !outDir) {
        process.stderr.write(USAGE);
        return 2;
    }

    const style = JSON.parse(readFileSync(input, 'utf8')) as MapboxStyle;
    const { mss, project, coverage } = convert(style, loadPropertyTable());

    mkdirSync(outDir, { recursive: true });
    writeFileSync(join(outDir, 'style.mss'), mss);
    writeFileSync(join(outDir, 'project.json'), project);
    process.stdout.write(`${coverage.report()}\n`);

    if (flags.has('--validate')) {
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

    if (flags.has('--strict') && coverage.droppedCount > 0) {
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
