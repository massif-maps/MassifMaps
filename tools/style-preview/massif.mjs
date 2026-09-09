/*
 * A Massif pane for the preview grid.
 *
 * The web build reads its style out of the module's own filesystem, and the demo's main() runs as
 * the module starts - so the converted CartoCSS project is written in `preRun`, and the tile source
 * and project name go through globalThis.MASSIF_DEFAULTS, which main() reads once. Two panes are
 * two modules, so they are built one after the other and the globals set immediately before each.
 */

const PROJECT = 'preview';

const IMAGE_PATH = /[\w./-]+\.(?:png|jpg|jpeg|svg)/g;

/**
 * Every file the project names. An icon reaches the stylesheet three different ways - a style
 * parameter, `url('...')`, and a bare quoted string inside a ternary - so the stylesheets are read
 * as text and every image-shaped path in them is taken, rather than each form parsed separately.
 */
async function projectFiles(base) {
    const project = await (await fetch(`${base}/project.json`)).json();
    const styles = project.styles ?? [];
    const texts = await Promise.all(styles.map((name) => fetch(`${base}/${name}`).then((r) => r.text())));
    const images = new Set(Object.values(project.styleparameters ?? {})
        .filter((v) => typeof v === 'string' && /\.(?:png|jpg|jpeg|svg)$/.test(v)));
    for (const text of texts) {
        for (const match of text.matchAll(IMAGE_PATH)) images.add(match[0]);
    }
    // The fonts the project carries. The decoder finds them by scanning the package for
    // <style>/fonts/, but a project served over HTTP cannot be listed, so project.json names them.
    const fonts = (project.fonts ?? []).map((name) => `fonts/${name}`);
    return ['project.json', ...styles, ...images, ...fonts];
}

/**
 * @param canvas   the pane's canvas
 * @param base     URL of the converted CartoCSS project
 * @param source   tile URL template the map reads
 * @param camera   {center: [lon, lat], zoom}
 * @param onError  called with each line the SDK writes to stderr - a style that names a font or an
 *                 icon the project does not carry says so there and nowhere else
 */
export async function createMassifPane(canvas, base, source, camera, onError = () => {}) {
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
        printErr: (line) => { console.error(line); onError(line); },
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
    // same as the demo's own page: leave the handles reachable from the console. Before the
    // camera, so a failure attaching one still leaves the module there to ask what went wrong.
    Object.assign(globalThis, { module, massif });
    const cam = await MassifCamera.attach(massif);
    globalThis.camera = cam;
    return {
        module,
        massif,
        camera: cam,
        // positional, the way the demo calls it: position, zoom, rotation, tilt, climbHeight
        moveTo: ({ center, zoom, bearing = 0, pitch = 0 }) =>
            massif.call(cam.handle, 'moveTo', [center, zoom, bearing, 90 - pitch, 0]),
    };
}
