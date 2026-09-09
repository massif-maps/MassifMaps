/*
 * Cross-origin isolation on a host that cannot set headers.
 *
 * The SDK's tile and label workers are pthreads, so the page needs SharedArrayBuffer, which needs
 * COOP/COEP. GitHub Pages serves no custom headers, so a service worker re-serves the documents
 * with them instead. Same file twice: loaded as a page script it registers itself, loaded as a
 * worker it does the rewriting.
 *
 * COEP is `credentialless`, not `require-corp`: a tile server does not send
 * Cross-Origin-Resource-Policy, and require-corp would block every tile.
 */

if (typeof window === 'undefined') {
  self.addEventListener('install', () => self.skipWaiting());
  self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

  self.addEventListener('fetch', (event) => {
    const request = event.request;
    if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') {
      return; // Chrome rejects this combination before it reaches us
    }
    event.respondWith(
      fetch(request)
        .then((response) => {
          if (response.status === 0) {
            return response; // opaque, nothing to rewrite
          }
          const headers = new Headers(response.headers);
          headers.set('Cross-Origin-Embedder-Policy', 'credentialless');
          headers.set('Cross-Origin-Opener-Policy', 'same-origin');
          return new Response(response.body, {
            status: response.status,
            statusText: response.statusText,
            headers,
          });
        })
        .catch((error) => {
          console.error('coi-serviceworker:', error);
          throw error;
        })
    );
  });
} else if (!window.crossOriginIsolated) {
  // One reload, once: without the guard a failed registration would reload forever.
  const RELOADED = 'coi-reloaded';
  if (!navigator.serviceWorker) {
    console.error('coi-serviceworker: no service worker support, the map needs SharedArrayBuffer');
  } else {
    navigator.serviceWorker
      .register(window.document.currentScript.src)
      // ready, not the register() result: right after registering the worker is still installing,
      // and only an ACTIVE one can serve the reload that makes the page isolated.
      .then(() => navigator.serviceWorker.ready)
      .then(() => {
        if (navigator.serviceWorker.controller) {
          return; // already serving this page, and it is still not isolated - nothing more to try
        }
        if (sessionStorage.getItem(RELOADED)) {
          console.error('coi-serviceworker: still not isolated after a reload, giving up');
          return;
        }
        sessionStorage.setItem(RELOADED, '1');
        window.location.reload();
      })
      .catch((error) => console.error('coi-serviceworker:', error));
  }
} else {
  sessionStorage.removeItem('coi-reloaded');
}
