"""Serves the web demo with the headers SharedArrayBuffer needs.

The SDK's tile pools are pthreads, so the page has to be cross-origin isolated - without
COOP/COEP the browser refuses SharedArrayBuffer and the module never starts.
"""

import argparse
import http.server
import os

class Handler(http.server.SimpleHTTPRequestHandler):
  def end_headers(self):
    if not args.noheaders:
      self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
      self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
    self.send_header('Cache-Control', 'no-store')
    super().end_headers()

parser = argparse.ArgumentParser()
parser.add_argument('--port', type=int, default=8088)
# web/, not web/demo: the page imports the binding from ../js.
parser.add_argument('--dir', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# GitHub Pages sends no custom headers, so this is how coi-serviceworker.js gets tested.
parser.add_argument('--no-headers', dest='noheaders', action='store_true',
                    help='Serve without COOP/COEP, the way a static host does')
args = parser.parse_args()

os.chdir(args.dir)
print('Serving %s - the map is at http://localhost:%d/demo/' % (args.dir, args.port))
http.server.test(HandlerClass=Handler, port=args.port, bind='127.0.0.1')
