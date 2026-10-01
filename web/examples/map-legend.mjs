/**
 * @title Map legend
 * @section styles
 * @order 40
 * A legend resolved from the live style: switch the Massif variant and every swatch follows, because
 * none of its colours is written down anywhere but in the style.
 */
import { massifStyle, vectorTiles } from './shared.mjs';

const VARIANTS = [['streets', 'Streets'], ['outdoor', 'Outdoor'], ['topo', 'Topo'], ['eink', 'E-ink']];
const W = 44;
const H = 18;

export default async function start(host) {
  const map = host.map;
  const spec = await massifStyle(map, 'outdoor');
  const style = map.style('massif', spec);
  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: 'massif' });
  map.camera().moveTo([5.7262, 45.2], { zoom: 13 });

  // Swatch images are paths inside the style's asset folder, which the module's filesystem holds.
  const images = new Map();
  const image = (file) => {
    if (!images.has(file)) {
      const bytes = map.module.FS.readFile(`${spec.project.assets.path}/${file}`);
      images.set(file, URL.createObjectURL(new Blob([bytes], { type: file.endsWith('.svg') ? 'image/svg+xml' : 'image/png' })));
    }
    return images.get(file);
  };

  const panel = document.createElement('div');
  panel.style.cssText = 'position:absolute;top:8px;right:8px;bottom:84px;width:230px;overflow:auto;background:#fffe;border-radius:8px;padding:4px 10px;font:12px system-ui,sans-serif;box-shadow:0 1px 4px #0004';
  host.root.appendChild(panel);

  function showLegend() {
    // getLegend reads the decoder's CURRENT parameters: call it again after any change.
    const legend = style.call('getLegend');
    panel.innerHTML = legend.sections.map((section) => `<h4 style="margin:10px 0 4px">${text(section.label)}</h4>`
      + section.items.map((item) => `<div style="display:flex;align-items:center;gap:8px;height:22px">`
        + `<svg width="${W}" height="${H}">${swatch(item, image)}</svg>${text(item.label)}</div>`).join('')).join('');
  }

  for (const [variant, label] of VARIANTS) {
    host.button(label, () => {
      style.set('params.variant', variant);
      showLegend();
      host.caption(`Massif ${label}: the legend was resolved again from the style.`);
    });
  }
  showLegend();
  host.caption('Massif Outdoor, and its legend: colours, widths and icons read from the compiled style.');
}

/** A label is a string or a {lang: text} object. */
function text(label) {
  return typeof label === 'object' && label ? (label.en ?? Object.values(label)[0]) : (label ?? '');
}

function swatch(item, image) {
  const cy = H / 2;
  switch (item.kind) {
    case 'line':
      return (item.lines ?? []).map((line) => `<line x1="0" y1="${cy + (line.offset ?? 0)}" x2="${W}" y2="${cy + (line.offset ?? 0)}" stroke="${line.color ?? '#000'}"`
        + ` stroke-width="${Math.min(line.width ?? 1, H - 2)}" stroke-opacity="${line.opacity ?? 1}" stroke-dasharray="${line.dasharray ?? ''}"/>`).join('');
    case 'fill':
      return `<rect width="${W}" height="${H}" fill="${item.color ?? 'none'}"`
        + (item.outline ? ` stroke="${item.outline.color}" stroke-width="${item.outline.width ?? 1}" stroke-dasharray="${item.outline.dasharray ?? ''}"` : '') + '/>'
        + (item.pattern ? `<image href="${image(item.pattern)}" width="${W}" height="${H}" preserveAspectRatio="xMidYMid slice"/>` : '');
    case 'shield':
      if (item.plate) {
        const p = item.plate;
        return `<rect x="1" y="2" width="${W - 2}" height="${H - 4}" rx="${Math.min(p.radius, H / 2 - 2)}" fill="${p.color}" stroke="${p.border ?? 'none'}" stroke-width="${p.borderWidth ?? 0}"/>`
          + (item.icon ? `<image href="${image(item.icon)}" x="${W / 2 - 7}" y="${cy - 7}" width="14" height="14"/>` : plateText(item.text));
      }
      return (item.icon ? `<image href="${image(item.icon)}" width="${W}" height="${H}"/>` : '') + plateText(item.text);
    case 'poi':
      if (item.icon) return `<image href="${image(item.icon)}" x="${W / 2 - 8}" y="${cy - 8}" width="16" height="16"/>`;
      if (item.marker) return `<circle cx="${W / 2}" cy="${cy}" r="${Math.max(item.marker.size, 3)}" fill="${item.marker.color}" stroke="${item.marker.border ?? 'none'}" stroke-width="${item.marker.borderWidth ?? 0}"/>`;
      return '';
    default:
      return item.text ? `<text x="${W / 2}" y="${cy}" text-anchor="middle" dominant-baseline="central" font-size="11" fill="${item.text.color}">${item.text.value}</text>` : '';
  }
}

function plateText(text) {
  return text ? `<text x="${W / 2}" y="${H / 2}" text-anchor="middle" dominant-baseline="central" font-size="10" font-weight="bold" fill="${text.color}">${text.value}</text>` : '';
}
