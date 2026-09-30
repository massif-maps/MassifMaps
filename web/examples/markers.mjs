/** Markers and a popup, both described entirely by JSON - no style builder anywhere. */
import { massifStyle, vectorTiles } from './shared.mjs';

const SUMMITS = [
  { name: 'Mont Blanc', at: [6.8652, 45.8326], metres: 4808 },
  { name: 'Grandes Jorasses', at: [6.9866, 45.8697], metres: 4208 },
  { name: 'Aiguille Verte', at: [6.9256, 45.9036], metres: 4122 },
  { name: 'Mont Dolent', at: [7.0575, 45.9264], metres: 3820 },
];

let popup = null;

export default async function start(host) {
  const map = host.map;

  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: await massifStyle(map) });

  // ONE style object shared by every marker - what matters once there are thousands of
  // them. A `style` key that is a STRING is looked up by id; an object is built inline.
  const pin = map.elements().style('pin', { type: 'marker', size: 26, color: 0xffe5484d, clickSize: 40 });

  for (const summit of SUMMITS) {
    map.addMarker({ type: 'marker', position: summit.at, style: pin.id });
  }

  map.camera().moveTo([6.94, 45.87], { zoom: 10.6 });

  map.elements().onClick((e) => {
    show(map, e.getPos('elementClickPos'));
    // Claim the tap, or the map's own onClick below fires for the SAME tap and dismisses
    // the popup as it opens.
    e.consumed = true;
  });
  map.onClick(() => dismiss(map));

  host.caption('Tap a pin. Everything here - marker, style, popup - is a JSON spec.');
}

/** A balloon at a position, built the same way a marker is. */
function show(map, at) {
  dismiss(map);
  if (!at) {
    return;
  }
  const summit = SUMMITS.find((s) => Math.abs(s.at[0] - at[0]) < 1e-4 && Math.abs(s.at[1] - at[1]) < 1e-4);
  popup = map.addPopup({
    type: 'balloon',
    position: at,
    title: summit?.name ?? 'Summit',
    description: `${summit?.metres ?? 0} m`,
    style: {
      type: 'balloon',
      cornerRadius: 6,
      leftColor: 0xffe5484d,
      titleFontSize: 14,
      descriptionFontSize: 12,
    },
  });
}

function dismiss(map) {
  if (popup) {
    map.elements().remove(popup);
    popup.destroy();
    popup = null;
  }
}
