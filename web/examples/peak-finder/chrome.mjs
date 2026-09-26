/*
 * The peak finder's controls, as the Alpimaps app lays them out (PeakFinderOverlay): the compass,
 * the selected summit's card, the sun bar and a settings drawer. Plain DOM - none of it is map code.
 */

const CSS = `
.pf { --primary: #4465be; --ink: #1f2430; --muted: #6b7280; --surface: #ffffffee;
      --shadow: 0 4px 18px #0000001f, 0 1px 3px #00000014; font: 13px/1.4 system-ui, sans-serif; color: var(--ink); }
.pf-fab { position: absolute; width: 44px; height: 44px; border-radius: 22px; border: 0; padding: 0; top: 12px; right: 12px;
          background: var(--surface); box-shadow: var(--shadow); color: var(--ink); cursor: pointer; display: flex;
          align-items: center; justify-content: center; z-index: 3; }
.pf-compass { position: absolute; top: 12px; left: 12px; width: 52px; padding: 6px 0 5px; border-radius: 26px;
              background: var(--surface); box-shadow: var(--shadow); cursor: pointer; display: flex; flex-direction: column;
              align-items: center; font: 600 11px/1.2 system-ui, sans-serif; user-select: none; }
.pf-compass svg { width: 30px; height: 30px; }
.pf-compass .cardinal { color: var(--muted); font-weight: 500; }
.pf-card { position: absolute; left: 50%; bottom: 84px; transform: translate(-50%, 12px); opacity: 0; pointer-events: none;
           transition: opacity .2s, transform .2s; display: flex; align-items: center; gap: 12px; background: var(--surface);
           border-radius: 20px; padding: 10px 10px 10px 14px; box-shadow: var(--shadow); max-width: calc(100% - 32px); box-sizing: border-box; }
.pf-card.open { opacity: 1; transform: translate(-50%, 0); pointer-events: auto; }
.pf-card .text { min-width: 0; }
.pf-card .name { font-weight: 700; font-size: 15px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.pf-card .meta { color: var(--muted); font-size: 12px; white-space: nowrap; font-variant-numeric: tabular-nums; }
.pf-card button, .pf-sun button { border: 0; cursor: pointer; font: 600 12px/1 system-ui, sans-serif; border-radius: 18px; height: 34px; padding: 0 14px; }
.pf-card .look { background: #4465be1a; color: var(--primary); }
.pf-card .fly { background: var(--primary); color: #fff; }
.pf-card .close { background: transparent; color: var(--muted); padding: 0 8px; }
.pf-sun { position: absolute; left: 12px; bottom: 28px; display: flex; align-items: center; gap: 10px; background: var(--surface);
          border-radius: 20px; padding: 8px 14px; box-shadow: var(--shadow); flex-wrap: wrap; max-width: calc(100% - 48px); }
.pf-sun input[type=date] { font: inherit; border: 0; background: transparent; color: var(--ink); }
.pf-sun input[type=range] { width: 140px; accent-color: #f59e0b; }
.pf-sun .time { font-weight: 700; font-variant-numeric: tabular-nums; min-width: 44px; }
.pf-sun .riseset { color: var(--muted); font-size: 12px; font-variant-numeric: tabular-nums; }
.pf-sun button { height: 28px; padding: 0 10px; background: #f59e0b1f; color: #b45309; }
.pf-sun button.off { background: transparent; color: var(--muted); box-shadow: inset 0 0 0 1px #0000001f; }
.pf-panel { position: absolute; top: 0; right: 0; bottom: 0; width: 280px; overflow: auto; box-sizing: border-box; font-size: 12px;
            background: #fffffff5; padding: 64px 16px 16px; box-shadow: var(--shadow); transform: translateX(105%);
            transition: transform .25s; z-index: 2; }
.pf-panel.open { transform: none; }
.pf-panel h4 { margin: 14px 0 6px; font-size: 11px; color: var(--muted); text-transform: uppercase; letter-spacing: .08em; }
.pf-panel label { display: block; margin-bottom: 6px; }
.pf-panel .row { display: flex; align-items: center; gap: 8px; }
.pf-panel input[type=range] { flex: 1; min-width: 0; accent-color: var(--primary); }
.pf-panel .val { width: 48px; text-align: right; color: var(--muted); font-variant-numeric: tabular-nums; }
.pf-panel a { display: block; margin-top: 12px; color: var(--primary); }
.pf-credit { position: absolute; right: 8px; bottom: 4px; font-size: 10px; color: #6b7280cc; }
.pf-credit a { color: inherit; }
`;

const COMPASS_POINTS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
export const toCompass = (heading) => COMPASS_POINTS[Math.round((((heading % 360) + 360) % 360) / 22.5) % 16];

const html = (parent, markup) => {
  parent.insertAdjacentHTML('beforeend', markup);
  return parent.lastElementChild;
};

/** Builds the controls into `root`; `on` receives the user's actions. */
export function createChrome(root, on) {
  root.classList.add('pf');
  html(document.head, `<style>${CSS}</style>`);

  const compass = html(root, `<div class="pf-compass" title="Look north">
    <svg viewBox="0 0 24 24"><path class="needle" fill="currentColor" d="M12 2 4.5 20.3l.7.7L12 18l6.8 3 .7-.7z"/></svg>
    <span class="heading">0°</span><span class="cardinal">N</span></div>`);
  compass.onclick = () => on.north();

  const card = html(root, `<div class="pf-card"><div class="text"><div class="name"></div><div class="meta"></div></div>
    <button class="look">Look at</button><button class="fly">Fly to</button><button class="close">✕</button></div>`);
  card.querySelector('.look').onclick = () => on.lookAt();
  card.querySelector('.fly').onclick = () => on.flyTo();
  card.querySelector('.close').onclick = () => on.select(null);

  const sun = html(root, `<div class="pf-sun"><input type="date" class="date"><input type="range" class="minute" min="0" max="1439">
    <span class="time"></span><span class="riseset"></span><button class="hours off">Hours</button><button class="now">Now</button></div>`);
  const date = sun.querySelector('.date');
  const minute = sun.querySelector('.minute');
  const hours = sun.querySelector('.hours');
  const pad = (value) => String(value).padStart(2, '0');
  const setNow = () => {
    const now = new Date();
    date.value = `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`;
    minute.value = String(now.getHours() * 60 + now.getMinutes());
  };
  setNow();
  sun.querySelector('.now').onclick = setNow;
  hours.onclick = () => hours.classList.toggle('off');

  const panel = html(root, '<div class="pf-panel"></div>');
  const toggle = html(root, `<button class="pf-fab" title="Settings"><svg viewBox="0 0 24 24" width="22" height="22" fill="none"
    stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/>
    <circle cx="10" cy="17" r="2"/></svg></button>`);
  toggle.onclick = () => panel.classList.toggle('open');
  addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      on.select(null);
      panel.classList.remove('open');
    }
  });
  const peakfinderLink = document.createElement('a');
  peakfinderLink.target = '_blank';
  peakfinderLink.textContent = 'this view on peakfinder.com';

  html(root, `<div class="pf-credit">terrain <a href="https://mapterhorn.com" target="_blank">Mapterhorn</a> · summits
    <a href="https://openfreemap.org" target="_blank">OpenFreeMap</a> © <a href="https://www.openmaptiles.org" target="_blank">OpenMapTiles</a>
    © <a href="https://www.openstreetmap.org/copyright" target="_blank">OpenStreetMap</a> contributors</div>`);

  let heading = NaN;
  return {
    /** A titled section of sliders in the drawer. */
    section(title) {
      html(panel, `<h4>${title}</h4>`);
    },
    slider(label, value, min, max, step, action) {
      const row = html(panel, `<label>${label}<div class="row"><input type="range" min="${min}" max="${max}" step="${step}"
        value="${value}"><span class="val">${value}</span></div></label>`);
      const input = row.querySelector('input');
      input.oninput = () => {
        row.querySelector('.val').textContent = input.value;
        action(Number(input.value));
      };
    },
    choice(label, value, options, action) {
      const row = html(panel, `<label>${label} <select>${options.map(([key, text]) =>
        `<option value="${key}"${key === value ? ' selected' : ''}>${text}</option>`).join('')}</select></label>`);
      row.querySelector('select').onchange = (event) => action(event.target.value);
    },
    finishPanel() {
      panel.append(peakfinderLink);
    },
    setPeakfinderLink(href) {
      peakfinderLink.href = href;
    },
    showHeading(next) {
      if (next === heading) {
        return;
      }
      heading = next;
      compass.querySelector('.needle').setAttribute('transform', `rotate(${-next} 12 12)`);
      compass.querySelector('.heading').textContent = `${Math.round(next) % 360}°`;
      compass.querySelector('.cardinal').textContent = toCompass(next);
    },
    showPeak(peak, meta) {
      card.classList.toggle('open', !!peak);
      if (peak) {
        card.querySelector('.name').textContent = peak.name;
        card.querySelector('.meta').textContent = meta;
      }
    },
    /** The day (local midnight, ms), the minute of it and whether the hour marks are on. */
    sunState() {
      const [y, m, d] = date.value.split('-').map(Number);
      sun.querySelector('.time').textContent = `${pad(Math.floor(minute.value / 60))}:${pad(minute.value % 60)}`;
      return { day: new Date(y, m - 1, d).getTime(), minute: Number(minute.value), hours: !hours.classList.contains('off') };
    },
    showRiseSet(text) {
      sun.querySelector('.riseset').textContent = text;
    },
  };
}
