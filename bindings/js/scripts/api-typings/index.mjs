#!/usr/bin/env node
import { mkdirSync, writeFileSync } from 'node:fs';
import * as path from 'node:path';
import { OUT_DIR, loadSchema } from './schema.mjs';
import { emitTypes } from './types.mjs';
import { emitRuntime } from './runtime.mjs';

const USAGE = `
Regenerate the surface API typings from the SDK's own API schema.

  node scripts/api-typings/index.mjs [--schema <massif-api.json>] [--out <dir>]

The schema is looked up in this order:
  --schema
  $MASSIF_SDK_HOME/docs/api/massif-api.json
  ../../docs/api/massif-api.json     (this plugin checked out inside the SDK)

Writes, into src/ui-massifmaps/api:
  massif-api.d.ts   types only - class names, property paths, specs, methods, events
  schema.ts         the runtime tables marshalling needs
`;

const opts = { schema: undefined, out: OUT_DIR };
const argv = process.argv.slice(2);
for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--schema') opts.schema = argv[++i];
    else if (argv[i] === '--out') opts.out = argv[++i];
    else if (argv[i] === '-h' || argv[i] === '--help') {
        console.log(USAGE);
        process.exit(0);
    } else {
        console.error(`unknown argument: ${argv[i]}`);
        console.log(USAGE);
        process.exit(1);
    }
}

try {
    const { file, schema } = loadSchema(opts.schema);
    console.log(`schema: ${file}`);

    mkdirSync(opts.out, { recursive: true });

    const types = emitTypes(schema);
    writeFileSync(path.join(opts.out, 'massif-api.d.ts'), types.text);

    const runtime = emitRuntime(schema);
    writeFileSync(path.join(opts.out, 'schema.ts'), runtime.text);

    console.log(`massif-api.d.ts  ${Object.keys(schema.classes).length} classes, ${types.paths} paths, ${schema.specs.length} specs, ${Object.keys(schema.enums).length} enums`);
    console.log(`schema.ts        ${runtime.rows} property rows`);
} catch (error) {
    console.error(error.message);
    process.exit(1);
}
