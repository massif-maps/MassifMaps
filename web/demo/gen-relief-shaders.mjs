/*
 * Generates web/demo/relief-shaders.js from alpimaps' reliefShaders.ts.
 *
 * ONE COPY OF THE SHADER, not two. The whole reason the panorama runs in a browser is to iterate on
 * this source; a hand-copied duplicate would drift from the app within a day and every finding would
 * then be about the wrong file. Run this after editing the TS, reload the page.
 *
 *   node web/demo/gen-relief-shaders.mjs [path/to/reliefShaders.ts]
 */
import { readFileSync, writeFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';

const source = process.argv[2] ??
  '/Volumes/dev/nativescript/alpimaps/app/mapModules/terrain/reliefShaders.ts';
const text = readFileSync(source, 'utf8');

/*
 * A template scanner that understands NESTED ARMS.
 *
 * The naive version counted { and } to find the end of a ${...}, which the GLSL inside the arms is
 * full of - so the interpolation looked closed early, the next arm-closing backtick ended the whole
 * template, and the shader came out truncated with no main(). Inside an interpolation the arms are
 * themselves backtick templates, so they are skipped WHOLE rather than scanned for braces.
 */
function readArm(text, i) {
  // i points at the opening backtick; returns [body, indexAfterClosingBacktick]
  let out = '';
  i++;
  while (i < text.length) {
    if (text[i] === '\\') { out += text[i] + text[i + 1]; i += 2; continue; }
    if (text[i] === '`') { return [out, i + 1]; }
    out += text[i];
    i++;
  }
  throw new Error('unterminated arm');
}

/** The body of the backtick template that starts at or after `startIndex`, arms kept verbatim. */
function template(text, startIndex) {
  let i = text.indexOf('`', startIndex) + 1;
  let out = '';
  while (i < text.length) {
    const ch = text[i];
    if (ch === '\\') { out += ch + text[i + 1]; i += 2; continue; }
    if (ch === '`') { return { body: out, end: i }; }
    if (ch === '$' && text[i + 1] === '{') {
      // Copy the interpolation verbatim, skipping its arms so their braces and backticks cannot be
      // mistaken for structure of the outer template.
      let j = i + 2;
      let expr = '';
      let depth = 1;
      while (j < text.length) {
        if (text[j] === '`') { const [arm, next] = readArm(text, j); expr += '`' + arm + '`'; j = next; continue; }
        if (text[j] === '{') depth++;
        if (text[j] === '}') { depth--; if (depth === 0) break; }
        expr += text[j];
        j++;
      }
      out += '${' + expr + '}';
      i = j + 1;
      continue;
    }
    out += ch;
    i++;
  }
  throw new Error('unterminated template');
}

// Named GLSL chunks (`const NAME = \`...\`;`) that a shader pulls in as `${NAME}`: inlined here, since
// neither extraction below evaluates the TypeScript.
const chunks = {};
for (const match of text.matchAll(/^const (\w+) = `/gm)) {
  chunks[match[1]] = readArm(text, match.index + match[0].length - 1)[0];
}
const inlineChunks = (glsl) => glsl.replace(/\$\{(\w+)\}/g, (whole, name) => {
  if (!(name in chunks)) throw new Error(`unknown chunk ${name}`);
  return chunks[name];
});

const surface = template(text, text.indexOf('export const RELIEF_SURFACE_SHADER')).body;
const outlineWhole = template(text, text.indexOf('export function reliefOutlineShader')).body;

// The outline shader is one template with `${normals ? A : B}` blocks. The panorama always runs the
// NORMALS variant (setTerrainNormalsRequired), so take that side and drop the other.
function pickNormals(body) {
  let out = '';
  let i = 0;
  while (i < body.length) {
    const open = body.indexOf('${', i);
    if (open < 0) { out += body.slice(i); break; }
    out += body.slice(i, open);
    let j = open + 2;
    let depth = 1;
    const arms = [];
    while (j < body.length) {
      if (body[j] === '`') { const [arm, next] = readArm(body, j); arms.push(arm); j = next; continue; }
      if (body[j] === '{') depth++;
      if (body[j] === '}') { depth--; if (depth === 0) break; }
      j++;
    }
    if (!arms.length) {
      out += body.slice(open, j + 1); // a named chunk, inlined afterwards
      i = j + 1;
      continue;
    }
    out += arms[0]; // the normals side
    i = j + 1;
  }
  return out;
}

// Colour uniforms cannot be set through the facade's float-only parameter path, so they become
// constants. The palette is the light one from RELIEF_PALETTE.
const COLOURS = {
  uPaperColor: [0.969, 0.969, 0.957, 1.0],
  uInkColor: [0.078, 0.078, 0.102, 1.0],
  uShadeColor: [0.078, 0.078, 0.102, 1.0]
};
function inlineColours(glsl) {
  let out = glsl;
  for (const [name, rgba] of Object.entries(COLOURS)) {
    out = out.replace(new RegExp(`uniform\\s+vec4\\s+${name}\\s*;`, 'g'),
      `const vec4 ${name} = vec4(${rgba.join(', ')});`);
  }
  return out;
}

const outline = inlineColours(inlineChunks(pickNormals(outlineWhole)));

// The DEPTH-ONLY outline (reliefDepthOutlineShader): a plain function, no template arms, so it is
// read between its own backticks rather than picked apart.
const depthStart = text.indexOf('export function reliefDepthOutlineShader()');
if (depthStart < 0) {
  throw new Error('reliefDepthOutlineShader not found');
}
const depthTick = text.indexOf('`', depthStart);
const [depthBody] = readArm(text, depthTick);
const depthOutline = inlineColours(inlineChunks(depthBody));

writeFileSync(new URL('./relief-shaders.js', import.meta.url),
  `// GENERATED by gen-relief-shaders.mjs from ${source} - do not edit.\n` +
  `export const RELIEF_SURFACE_SHADER = ${JSON.stringify(inlineColours(surface))};\n` +
  `export const RELIEF_OUTLINE_SHADER = ${JSON.stringify(outline)};\n` +
  `export const RELIEF_DEPTH_OUTLINE_SHADER = ${JSON.stringify(depthOutline)};\n`);
console.log(`surface ${surface.length} chars, outline ${outline.length} chars, depth outline ${depthOutline.length} chars`);

// The SUMMIT LABELS' style, from the app's own peaksStyle.ts - the same one-copy rule. Its palette
// comes from reliefShaders.ts, which imports the app's e-ink test; outside the app there is no
// e-ink, so that import becomes a constant and both files are emitted type-stripped, as one module.
const peaksSource = source.replace(/reliefShaders\.ts$/, 'peaksStyle.ts');
const withoutImports = (ts) => ts.replace(/^import [^;]*;\n/gm, '');
writeFileSync(new URL('./peaks-style.js', import.meta.url),
  `// GENERATED by gen-relief-shaders.mjs from ${source} and ${peaksSource} - do not edit.\n` +
  'const isEInk = false;\n' +
  stripTypeScriptTypes(withoutImports(text)) + '\n' +
  stripTypeScriptTypes(withoutImports(readFileSync(peaksSource, 'utf8'))));
console.log('peaks style written');
