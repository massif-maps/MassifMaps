/*
 * A Massif pane for the preview grid.
 *
 * The web build reads its style out of the module's own filesystem, and the demo's main() runs as
 * the module starts - so the converted CartoCSS project is written in `preRun`, and the tile source
 * and project name go through globalThis.MASSIF_DEFAULTS, which main() reads once. Two panes are
 * two modules, so they are built one after the other and the globals set immediately before each.
 */

const PROJECT = 'preview';

/** Every file the project names: its stylesheets, and the icons its parameters point at. */
async function projectFiles(base) {
    const project = await (await fetch(`${base}/project.json`)).json();
    const icons = Object.values(project.styleparameters ?? {})
        .filter((v) => typeof v === 'string' && /\.(png|jpg|svg)$/.test(v));
    return ['project.json', ...(project.styles ?? []), ...new Set(icons)];
}

/**
 * @param canvas   the pane's canvas
 * @param base     URL of the converted CartoCSS project
 * @param source   tile URL template the map reads
 * @param camera   {center: [lon, lat], zoom}
 */
export async function createMassifPane(canvas, base, source, camera) {
    const names = await projectFiles(base);
    const files = await Promise.all(names.map(async (name) => [name,
        new Uint8Array(await (await fetch(`${base}/${name}`)).arrayBuffer())]));

    globalThis.MASSIF_DEFAULTS = {
        source,
        project: PROJECT,
        style: 'project',
        zoom: camera.zoom,
        lon: camera.center[0],
        lat: camera.center[1],
    };

    const { default: Module } = await import('/massif/demo/massif-demo.mjs');
    const module = await Module({
        canvas,
        // the module is imported from a path the document does not sit under, and emscripten
        // resolves its .wasm and .data against the DOCUMENT unless told otherwise
        locateFile: (file) => `/massif/demo/${file}`,
        preRun: [({ FS }) => {
            for (const [name, bytes] of files) {
                const at = `/styles/${PROJECT}/${name}`;
                FS.mkdirTree(at.slice(0, at.lastIndexOf('/')));
                FS.writeFile(at, bytes);
            }
        }],
    });

    const { Massif, MassifCamera } = await import('/massif/js/massif.mjs');
    const massif = new Massif(module);
    // same as the demo's own page: leave the handles reachable from the console
    Object.assign(globalThis, { module, massif });
    const cam = await MassifCamera.attach(massif);
    return {
        module,
        massif,
        camera: cam,
        // positional, the way the demo calls it: position, zoom, rotation, tilt, climbHeight
        moveTo: ({ center, zoom, bearing = 0, pitch = 0 }) =>
            massif.call(cam.handle, 'moveTo', [center, zoom, bearing, 90 - pitch, 0]),
    };
}
