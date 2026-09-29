/**
 * Draws a resolved legend (MBVectorTileDecoder::getLegend, `massif-style legend`) as an SVG sheet.
 * No filesystem access, so a browser can reuse it: images go through `imageHref`.
 */

export type LegendValue = string | number | boolean | null | LegendValue[] | { [key: string]: LegendValue };

export interface LegendLine {
    color?: string;
    width?: number;
    dasharray?: number[];
    offset?: number;
    opacity?: number;
    pattern?: string;
}

export interface LegendText {
    value: string;
    color: string;
    halo?: string;
    haloWidth?: number;
}

export interface LegendItem {
    id: string;
    label?: LegendValue;
    kind: 'line' | 'fill' | 'poi' | 'shield' | 'label';
    lines?: LegendLine[];
    color?: string;
    opacity?: number;
    pattern?: string;
    outline?: LegendLine;
    icon?: string;
    iconColor?: string;
    marker?: { color: string; size: number; border?: string; borderWidth?: number };
    glyph?: { char: string; font: string; color: string };
    text?: LegendText;
    plate?: { color: string; border?: string; borderWidth?: number; radius: number };
}

export interface LegendSection {
    id?: string;
    label?: LegendValue;
    items: LegendItem[];
}

export interface Legend {
    title?: LegendValue;
    sections: LegendSection[];
}

export interface LegendSvgOptions {
    /** Picks a label out of a {lang: text} object; the first one when absent. */
    lang?: string;
    /** Where an image file the style names is read from; the path itself by default. */
    imageHref?: (file: string) => string;
}

const WIDTH = 340;
const ROW = 28;
const SWATCH_W = 52;
const SWATCH_H = 22;
const HEADER = 30;
const PAD = 12;
const FONT = 'font-family="sans-serif"';

function esc(text: string | number): string {
    return String(text).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;' }[c]!));
}

export function labelText(label: LegendValue | undefined, lang?: string): string {
    if (label === undefined || label === null) return '';
    if (typeof label === 'object' && !Array.isArray(label)) {
        const text = (lang !== undefined ? label[lang] : undefined) ?? Object.values(label)[0];
        return text === undefined ? '' : String(text);
    }
    return String(label);
}

/** Widths to scale, up to what the swatch holds, so a motorway still reads wider than a track. */
function clampWidth(width: number): number {
    return Math.max(0.5, Math.min(width, SWATCH_H - 2));
}

class Sheet {
    private defs = '';
    private patterns = new Map<string, string>();

    constructor(private readonly href: (file: string) => string) {}

    pattern(file: string): string {
        let id = this.patterns.get(file);
        if (!id) {
            id = `pattern${this.patterns.size}`;
            this.patterns.set(file, id);
            this.defs += `<pattern id="${id}" patternUnits="userSpaceOnUse" width="12" height="12">`
                + `<image href="${esc(this.href(file))}" width="12" height="12"/></pattern>`;
        }
        return `url(#${id})`;
    }

    image(file: string, x: number, y: number, size: number): string {
        return `<image href="${esc(this.href(file))}" x="${x}" y="${y}" width="${size}" height="${size}"/>`;
    }

    wrap(body: string): string {
        return this.defs ? `<defs>${this.defs}</defs>${body}` : body;
    }
}

function text(t: LegendText, x: number, y: number, size: number, weight = 'bold'): string {
    const halo = t.halo && t.haloWidth ? ` stroke="${esc(t.halo)}" stroke-width="${2 * t.haloWidth}" paint-order="stroke"` : '';
    return `<text x="${x}" y="${y}" text-anchor="middle" dominant-baseline="central" ${FONT} font-size="${size}"`
        + ` font-weight="${weight}" fill="${esc(t.color)}"${halo}>${esc(t.value)}</text>`;
}

function drawSwatch(item: LegendItem, x: number, y: number, sheet: Sheet): string {
    const cx = x + SWATCH_W / 2;
    const cy = y + SWATCH_H / 2;
    switch (item.kind) {
        case 'line':
            return (item.lines ?? []).map((line) => {
                if (line.pattern) {
                    return `<rect x="${x}" y="${cy - 3}" width="${SWATCH_W}" height="6" fill="${sheet.pattern(line.pattern)}" opacity="${line.opacity ?? 1}"/>`;
                }
                const ly = cy + (line.offset ?? 0);
                const dash = line.dasharray ? ` stroke-dasharray="${line.dasharray.join(',')}"` : '';
                return `<line x1="${x}" y1="${ly}" x2="${x + SWATCH_W}" y2="${ly}" stroke="${esc(line.color ?? '#000')}"`
                    + ` stroke-width="${clampWidth(line.width ?? 1)}" stroke-opacity="${line.opacity ?? 1}"${dash}/>`;
            }).join('');
        case 'fill': {
            let out = '';
            if (item.color) out += `<rect x="${x}" y="${y}" width="${SWATCH_W}" height="${SWATCH_H}" fill="${esc(item.color)}" fill-opacity="${item.opacity ?? 1}"/>`;
            if (item.pattern) out += `<rect x="${x}" y="${y}" width="${SWATCH_W}" height="${SWATCH_H}" fill="${sheet.pattern(item.pattern)}"/>`;
            if (item.outline) {
                const o = item.outline;
                out += `<rect x="${x}" y="${y}" width="${SWATCH_W}" height="${SWATCH_H}" fill="none" stroke="${esc(o.color ?? '#000')}"`
                    + ` stroke-width="${o.width ?? 1}"${o.dasharray ? ` stroke-dasharray="${o.dasharray.join(',')}"` : ''}/>`;
            }
            return out;
        }
        case 'shield': {
            let out = '';
            const value = item.text?.value ?? '';
            if (item.plate) {
                const w = Math.min(SWATCH_W, 10 + value.length * 6.5);
                const p = item.plate;
                out += `<rect x="${cx - w / 2}" y="${y + 3}" width="${w}" height="${SWATCH_H - 6}" rx="${p.radius}" fill="${esc(p.color)}"`
                    + (p.border && p.borderWidth ? ` stroke="${esc(p.border)}" stroke-width="${p.borderWidth}"` : '') + '/>';
            }
            else if (item.icon) {
                out += sheet.image(item.icon, cx - SWATCH_H / 2, y, SWATCH_H);
            }
            if (item.text) out += text({ ...item.text, halo: undefined }, cx, cy, 10);
            return out;
        }
        case 'poi':
            if (item.icon) return sheet.image(item.icon, cx - 9, cy - 9, 18);
            if (item.marker) {
                const m = item.marker;
                return `<circle cx="${cx}" cy="${cy}" r="${Math.min(m.size, SWATCH_H) / 2}" fill="${esc(m.color)}"`
                    + (m.border && m.borderWidth ? ` stroke="${esc(m.border)}" stroke-width="${m.borderWidth}"` : '') + '/>';
            }
            if (item.glyph) {
                return `<text x="${cx}" y="${cy}" text-anchor="middle" dominant-baseline="central" font-family="${esc(item.glyph.font)}"`
                    + ` font-size="16" fill="${esc(item.glyph.color)}">${esc(item.glyph.char)}</text>`;
            }
            return '';
        case 'label':
            return item.text ? text(item.text, cx, cy, 12) : '';
        default:
            return '';
    }
}

export function renderLegendSvg(legend: Legend, options: LegendSvgOptions = {}): string {
    const sheet = new Sheet(options.imageHref ?? ((file: string) => file));
    let y = PAD;
    let body = '';
    const title = labelText(legend.title, options.lang);
    if (title) {
        body += `<text x="${PAD}" y="${y + 16}" ${FONT} font-size="17" font-weight="bold">${esc(title)}</text>`;
        y += HEADER + 4;
    }
    for (const section of legend.sections) {
        body += `<text x="${PAD}" y="${y + 18}" ${FONT} font-size="13" font-weight="bold">${esc(labelText(section.label, options.lang))}</text>`;
        y += HEADER;
        for (const item of section.items) {
            body += drawSwatch(item, PAD, y, sheet)
                + `<text x="${PAD + SWATCH_W + PAD}" y="${y + SWATCH_H / 2}" dominant-baseline="central" ${FONT} font-size="12">`
                + `${esc(labelText(item.label, options.lang))}</text>`;
            y += ROW;
        }
    }
    const height = y + PAD;
    return `<svg xmlns="http://www.w3.org/2000/svg" width="${WIDTH}" height="${height}" viewBox="0 0 ${WIDTH} ${height}">`
        + `<rect width="100%" height="100%" fill="#ffffff"/>${sheet.wrap(body)}</svg>\n`;
}
