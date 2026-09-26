/*
 * The page an example runs on - the web twin of the Android app's ExampleHost and the NativeScript
 * one (demo-snippets/svelte/examples/host.ts), so an example file reads as map code alone.
 */

/** Builds the host over `root`, which holds the map canvas; controls and caption are added to it. */
export function createHost(root, map) {
  const bar = element('div', 'example-controls', root);
  const caption = element('div', 'example-caption', root);
  caption.hidden = true;

  return {
    map,
    /** The element over the map, for an example that brings its own controls. */
    root,
    caption(text) {
      caption.textContent = text;
      caption.hidden = !text;
    },
    button(label, action) {
      element('button', '', bar, label).addEventListener('click', () => action());
    },
    toggle(label, on, action) {
      const button = element('button', on ? 'on' : '', bar, label);
      button.addEventListener('click', () => {
        on = !on;
        button.className = on ? 'on' : '';
        action(on);
      });
    },
    slider(label, min, max, value, action) {
      const row = element('label', 'example-slider', bar);
      const text = element('span', '', row);
      const input = element('input', '', row);
      Object.assign(input, { type: 'range', min, max, step: (max - min) / 200, value });
      const show = () => (text.textContent = `${label} ${Number(input.value).toFixed(2)}`);
      show();
      input.addEventListener('input', () => {
        show();
        action(Number(input.value));
      });
    },
    after(millis, action) {
      setTimeout(action, millis);
    },
  };
}

function element(tag, className, parent, text) {
  const node = document.createElement(tag);
  node.className = className;
  if (text) {
    node.textContent = text;
  }
  parent.appendChild(node);
  return node;
}
