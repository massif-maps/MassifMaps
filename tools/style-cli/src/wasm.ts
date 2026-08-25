import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
// dist/wasm.js -> ../wasm; src/wasm.ts -> ../../wasm when running from source.
const CANDIDATES = [join(HERE, '..', 'wasm'), join(HERE, '..', '..', 'wasm')];

export class WasmMissing extends Error {}

interface StyleModule {
    callMain(args: string[]): number;
}

let cached: Promise<StyleModule> | null = null;

/**
 * Loads massif-style.mjs, the C++ tools compiled with NODERAWFS - so the module reads and writes
 * real paths and needs no virtual filesystem plumbing here.
 */
export async function loadStyleModule(): Promise<StyleModule> {
    if (cached) return cached;
    const dir = CANDIDATES.find((c) => existsSync(join(c, 'massif-style.mjs')));
    if (!dir) {
        throw new WasmMissing(
            'massif-style.mjs not found. Build it from the libs-massif submodule, or download it ' +
            'from a style-tools-v* release into tools/style-cli/wasm/. ' +
            'See docs/contributing/style-tools.md.',
        );
    }
    cached = import(join(dir, 'massif-style.mjs')).then((mod) => mod.default());
    return cached;
}

/** Runs a wasm subcommand. Returns its exit code; EXIT_RUNTIME makes it throw ExitStatus instead. */
export async function runWasm(args: string[]): Promise<number> {
    const mod = await loadStyleModule();
    try {
        return mod.callMain(args) ?? 0;
    } catch (error) {
        const status = (error as { status?: number }).status;
        if (typeof status === 'number') return status;
        throw error;
    }
}

/** Whether --validate can run at all: the wasm is optional when only translating. */
export function wasmAvailable(): boolean {
    return CANDIDATES.some((c) => existsSync(join(c, 'massif-style.mjs')));
}
