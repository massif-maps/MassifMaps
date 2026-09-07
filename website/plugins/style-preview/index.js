/**
 * style-preview plugin — cross-origin isolation for `docusaurus start`.
 *
 * The SDK's tile pools are pthreads, so the wasm module the /preview page runs needs
 * SharedArrayBuffer, which a browser only hands to a cross-origin-isolated page. The dev server
 * sends no such headers by default, so without this the preview never starts locally.
 *
 * `devServer` on the webpack config is the hook for it: Docusaurus merges it over its own defaults
 * (see @docusaurus/core commands/start/webpack.js). In production GitHub Pages sends no custom
 * headers either, and there the page falls back to static/coi-serviceworker.js.
 */

module.exports = function stylePreviewPlugin() {
  return {
    name: 'style-preview',

    configureWebpack(_config, isServer) {
      if (isServer) return {};
      return {
        devServer: {
          headers: {
            // The wasm under static/preview is rebuilt far more often than the site, and its URL
            // never changes - so the browser happily serves yesterday's renderer against today's
            // page, which reads as "my change did nothing". Cost nothing but an afternoon once.
            'Cache-Control': 'no-store',
            'Cross-Origin-Opener-Policy': 'same-origin',
            // `credentialless`, matching what static/coi-serviceworker.js sets in production: a
            // tile server does not send Cross-Origin-Resource-Policy, and require-corp would
            // block every tile. Locally and deployed have to agree, or one of them lies.
            'Cross-Origin-Embedder-Policy': 'credentialless',
          },
        },
      };
    },
  };
};
