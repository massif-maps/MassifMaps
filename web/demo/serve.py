"""Serves the web demo with the headers SharedArrayBuffer needs.

The SDK's tile pools are pthreads, so the page has to be cross-origin isolated - without
COOP/COEP the browser refuses SharedArrayBuffer and the module never starts.
"""

import argparse
import http.server
import os

class Handler(http.server.SimpleHTTPRequestHandler):
  def end_headers(self):
    self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
    self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
    self.send_header('Cache-Control', 'no-store')
    super().end_headers()

parser = argparse.ArgumentParser()
parser.add_argument('--port', type=int, default=8088)
parser.add_argument('--dir', default=os.path.dirname(os.path.abspath(__file__)))
args = parser.parse_args()

os.chdir(args.dir)
print('Serving %s on http://localhost:%d' % (args.dir, args.port))
http.server.test(HandlerClass=Handler, port=args.port, bind='127.0.0.1')
