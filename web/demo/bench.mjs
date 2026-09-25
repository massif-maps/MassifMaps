#!/usr/bin/env node
/*
 * Drives the panorama bench in a headless browser over the DevTools protocol.
 *
 * --dump-dom and --screenshot cannot do what this needs: they run a page once, take one artefact and
 * exit, with no console and no way to ask the page a question afterwards. The peak finder's fault is
 * RUN TO RUN, so the bench has to load the same URL twice and compare - and when a frame is wrong,
 * the next question is always "what does the page say about it", which means console plus evaluate.
 *
 * Node 26 ships a WebSocket client, so this needs no dependencies at all.
 *
 *   node bench.mjs --out /tmp/a.png --wait 25000
 *   node bench.mjs --url '...&debug=13' --eval 'globalThis.__net.count'
 */

import { spawn } from 'node:child_process';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const CHROMIUM = process.env.CHROMIUM ?? '/Applications/Chromium.app/Contents/MacOS/Chromium';
const DEFAULT_URL = 'http://localhost:8099/demo/panorama.html';

function parseArgs(argv) {
    const args = { wait: 25000, out: null, url: DEFAULT_URL, eval: [], step: [], width: 900, height: 600, scale: 0, keep: false };
    for (let i = 0; i < argv.length; i += 1) {
        const key = argv[i].replace(/^--/, '');
        if (key === 'eval') { args.eval.push(argv[++i]); } else if (key === 'step') { args.step.push(argv[++i]); } else if (key === 'keep') { args.keep = true; } else if (key in args) { args[key] = argv[++i]; }
    }
    args.wait = Number(args.wait);
    return args;
}

/** One CDP session: request/response by id, plus events by method. */
class Devtools {
    constructor(socket) {
        this.socket = socket;
        this.nextId = 1;
        this.pending = new Map();
        this.listeners = [];
        socket.addEventListener('message', (event) => {
            const message = JSON.parse(event.data);
            if (message.id && this.pending.has(message.id)) {
                const { resolve, reject } = this.pending.get(message.id);
                this.pending.delete(message.id);
                message.error ? reject(new Error(JSON.stringify(message.error))) : resolve(message.result);
            } else if (message.method) {
                this.listeners.forEach((fn) => fn(message));
            }
        });
    }

    static async connect(url) {
        const socket = new WebSocket(url);
        await new Promise((resolve, reject) => {
            socket.addEventListener('open', resolve, { once: true });
            socket.addEventListener('error', reject, { once: true });
        });
        return new Devtools(socket);
    }

    send(method, params = {}, sessionId) {
        const id = this.nextId++;
        this.socket.send(JSON.stringify({ id, method, params, sessionId }));
        return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
    }

    on(fn) { this.listeners.push(fn); }
}

async function waitForEndpoint(port, timeoutMs = 20000) {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
        try {
            const response = await fetch(`http://127.0.0.1:${port}/json/version`);
            if (response.ok) { return (await response.json()).webSocketDebuggerUrl; }
        } catch (error) { /* not up yet */ }
        if (Date.now() > deadline) { throw new Error('devtools endpoint never came up'); }
        await new Promise((resolve) => setTimeout(resolve, 150));
    }
}

const args = parseArgs(process.argv.slice(2));
const port = 9300 + Math.floor(Math.random() * 400);
const profile = await mkdtemp(join(tmpdir(), 'massif-bench-'));

const chromium = spawn(CHROMIUM, [
    '--headless=new',
    // SwiftShader: no GPU in a headless run, and the terrain path is GL - without this there is no
    // context at all and every frame is the clear colour.
    '--enable-unsafe-swiftshader',
    '--disable-gpu-sandbox',
    '--hide-scrollbars',
    `--remote-debugging-port=${port}`,
    `--user-data-dir=${profile}`,
    `--window-size=${args.width},${args.height}`,
    // --scale 2: a retina screen, where the page's device-pixel sizes are what differ
    ...(Number(args.scale) > 0 ? [`--force-device-scale-factor=${args.scale}`] : []),
    'about:blank'
], { stdio: ['ignore', 'ignore', 'pipe'] });
chromium.stderr.on('data', () => { /* chromium is noisy on stderr; the page's console is what matters */ });

const browser = await Devtools.connect(await waitForEndpoint(port));
const { targetId } = await browser.send('Target.createTarget', { url: 'about:blank' });
const { sessionId } = await browser.send('Target.attachToTarget', { targetId, flatten: true });

const lines = [];
browser.on((message) => {
    if (message.method === 'Runtime.consoleAPICalled') {
        const text = message.params.args.map((a) => a.value ?? a.description ?? a.unserializableValue ?? '').join(' ');
        lines.push(`${message.params.type}: ${text}`);
    } else if (message.method === 'Runtime.exceptionThrown') {
        const details = message.params.exceptionDetails;
        lines.push(`EXCEPTION: ${details.exception?.description ?? details.text}`);
    } else if (message.method === 'Log.entryAdded') {
        lines.push(`${message.params.entry.source}/${message.params.entry.level}: ${message.params.entry.text}`);
    }
});

await browser.send('Runtime.enable', {}, sessionId);
await browser.send('Log.enable', {}, sessionId);
await browser.send('Page.enable', {}, sessionId);
await browser.send('Page.navigate', { url: args.url }, sessionId);
await new Promise((resolve) => setTimeout(resolve, args.wait));

const evaluate = async (expression) => {
    const result = await browser.send('Runtime.evaluate',
        { expression, awaitPromise: true, returnByValue: true }, sessionId);
    return result.exceptionDetails ? `THREW ${result.exceptionDetails.text}` : result.result.value;
};

console.log('=== console');
console.log(lines.join('\n') || '(nothing)');
console.log('=== net');
console.log(JSON.stringify(await evaluate('globalThis.__net && {count: __net.count, byHost: __net.byHost, status: __net.status, last: __net.last.slice(0,12)}'), null, 1));
console.log('=== hud');
console.log(await evaluate('document.getElementById("hud").textContent'));
for (const expression of args.eval) {
    console.log(`=== eval ${expression}`);
    console.log(JSON.stringify(await evaluate(expression), null, 1));
}

const capture = async (path) => {
    const shot = await browser.send('Page.captureScreenshot', { format: 'png' }, sessionId);
    await writeFile(path, Buffer.from(shot.data, 'base64'));
    console.log(`=== shot ${path}`);
};

if (args.out) {
    await capture(args.out);
}

// --step lets one run drive a SEQUENCE, which is what the round-trip test needs: shoot the view,
// pan away, come back, shoot it again. Two page loads cannot ask that question - the fault is
// whether returning to a camera reproduces the frame it first drew.
for (const step of args.step) {
    const [kind, ...rest] = step.split(':');
    const value = rest.join(':');
    if (kind === 'wait') {
        await new Promise((resolve) => setTimeout(resolve, Number(value)));
    } else if (kind === 'shot') {
        await capture(value);
    } else if (kind === 'eval') {
        console.log(`=== step eval ${value} -> ${JSON.stringify(await evaluate(value))}`);
    }
}

if (!args.keep) {
    chromium.kill();
    await rm(profile, { recursive: true, force: true });
}
process.exit(0);
