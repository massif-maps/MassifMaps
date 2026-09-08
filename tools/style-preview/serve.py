#!/usr/bin/env python3
"""Static server + MBTiles tile endpoints for the two-pane style preview."""

import argparse
import gzip
import json
import os
import sqlite3
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.abspath(__file__))


class Tileset:
    def __init__(self, name, path):
        self.name = name
        self.path = path
        self.local = threading.local()
        meta = dict(self._db().execute("select name, value from metadata").fetchall())
        self.meta = meta
        self.gzipped = meta.get("compression", "gzip") == "gzip"

    def _db(self):
        conn = getattr(self.local, "conn", None)
        if conn is None:
            conn = sqlite3.connect("file:%s?mode=ro" % self.path, uri=True, check_same_thread=False)
            self.local.conn = conn
        return conn

    def tile(self, z, x, y):
        row = (1 << z) - 1 - y
        got = self._db().execute(
            "select tile_data from tiles where zoom_level=? and tile_column=? and tile_row=?",
            (z, x, row),
        ).fetchone()
        return got[0] if got else None

    def tilejson(self, base):
        meta = self.meta
        out = json.loads(meta.get("json", "{}"))
        out.update({
            "tilejson": "3.0.0",
            "tiles": ["%s/tiles/%s/{z}/{x}/{y}.pbf" % (base, self.name)],
            "name": meta.get("name", self.name),
            "attribution": meta.get("attribution", ""),
            "minzoom": int(meta.get("minzoom", 0)),
            "maxzoom": int(meta.get("maxzoom", 14)),
        })
        for key in ("bounds", "center"):
            if key in meta:
                out[key] = [float(v) for v in meta[key].split(",")]
        return out


class Handler(SimpleHTTPRequestHandler):
    tilesets = {}
    styles_dir = None
    massif_dir = None
    isolate = False

    def __init__(self, *a, **kw):
        super().__init__(*a, directory=ROOT, **kw)

    def translate_path(self, path):
        # a style project and the web build live outside this folder, so mount them
        clean = path.split("?")[0]
        for prefix, root in (("/styles/", self.styles_dir), ("/massif/", self.massif_dir)):
            if root and clean.startswith(prefix):
                rel = os.path.normpath(clean[len(prefix):]).lstrip("/.")
                return os.path.join(root, rel)
        return super().translate_path(path)

    def log_message(self, fmt, *args):
        # log_error passes an HTTPStatus here, so this cannot assume the request line
        if "/tiles/" not in str(args[0] if args else ""):
            super().log_message(fmt, *args)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        # every file here is being edited; a cached wasm or style is only ever confusing
        self.send_header("Cache-Control", "no-store")
        if self.isolate:
            # the SDK's tile pools are pthreads, so the page needs SharedArrayBuffer
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        super().end_headers()

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/tilesets.json":
            return self._json({n: t.meta.get("name", n) for n, t in self.tilesets.items()})
        parts = path.strip("/").split("/")
        if len(parts) == 2 and parts[0] == "tiles" and parts[1].endswith(".json"):
            name = parts[1][:-5]
            if name in self.tilesets:
                base = "http://%s" % self.headers.get("Host", "localhost")
                return self._json(self.tilesets[name].tilejson(base))
        if len(parts) == 5 and parts[0] == "tiles" and parts[4].endswith(".pbf"):
            return self._tile(parts[1], parts[2], parts[3], parts[4][:-4])
        return super().do_GET()

    def _json(self, obj):
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _tile(self, name, z, x, y):
        ts = self.tilesets.get(name)
        if ts is None:
            return self.send_error(404)
        try:
            data = ts.tile(int(z), int(x), int(y))
        except ValueError:
            return self.send_error(400)
        if data is None:
            self.send_response(204)
            self.send_header("Content-Length", "0")
            return self.end_headers()
        # some archives gzip only part of their tiles, so trust the magic over the metadata
        gzipped = data[:2] == b"\x1f\x8b"
        self.send_response(200)
        self.send_header("Content-Type", "application/x-protobuf")
        if gzipped:
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--mbtiles", action="append", default=[], metavar="NAME=PATH",
                    help="register an archive at /tiles/NAME (repeatable)")
    ap.add_argument("--styles", metavar="DIR", default=os.path.join(ROOT, "..", "..", "styles"),
                    help="folder served at /styles, holding the style projects")
    ap.add_argument("--massif", metavar="DIR", default=os.path.join(ROOT, "..", "..", "web"),
                    help="the web build, served at /massif; needs massif-demo.wasm in its demo/")
    ap.add_argument("--no-isolate", dest="isolate", action="store_false",
                    help="drop COOP/COEP, which the Massif panes need but a strict CDN dislikes")
    args = ap.parse_args()

    Handler.styles_dir = os.path.abspath(args.styles)
    Handler.massif_dir = os.path.abspath(args.massif)
    Handler.isolate = args.isolate

    for spec in args.mbtiles:
        name, _, path = spec.partition("=")
        if not path:
            ap.error("--mbtiles takes NAME=PATH, got %r" % spec)
        path = os.path.abspath(os.path.expanduser(path))
        if not os.path.exists(path):
            ap.error("no such archive: %s" % path)
        Handler.tilesets[name] = Tileset(name, path)
        print("  /tiles/%s  <-  %s" % (name, path))

    print("http://127.0.0.1:%d" % args.port)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
