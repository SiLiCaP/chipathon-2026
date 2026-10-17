#!/usr/bin/env python3
"""Serve the local-ci results directory.

usage: serve.py <dir> <port>

Plain static file server, plus:
  * CORS and Private Network Access headers for the online viewers only (Tiny
    Tapeout 3D viewer / Explorer, Surfer), so they can load layouts and waveforms
    from localhost; other web pages cannot read the results
  * <dir>/?zip=1 streams that directory as a .zip (like downloading a GitHub artifact)
"""

import functools
import http.server
import os
import sys
import urllib.parse
import zipfile

# Sites that fetch from this server (see the viewer links in pipeline.sh / report.py)
ALLOWED_ORIGINS = {
    "https://gds-viewer.tinytapeout.com",
    "https://gds-explorer.tinytapeout.com",
    "https://app.surfer-project.org",
}


class _Stream:
    """Write-only, unseekable file for zipfile: it then writes data descriptors
    instead of seeking back, so the archive can go straight to the socket."""

    def __init__(self, out):
        self.out, self.pos = out, 0

    def write(self, b):
        self.out.write(b)
        self.pos += len(b)
        return len(b)

    def tell(self):
        return self.pos

    def flush(self):
        self.out.flush()


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        origin = self.headers.get("Origin")
        if origin in ALLOWED_ORIGINS:
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Vary", "Origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        if "zip=1" in url.query.split("&"):
            local = self.translate_path(url.path)
            if os.path.isdir(local):
                return self.send_zip(local)
        return super().do_GET()

    def send_zip(self, local):
        # Streamed: no Content-Length, the end of the response is the end of the
        # connection (HTTP/1.0), so large artifacts never sit in memory.
        name = os.path.basename(os.path.normpath(local)) or "artifact"
        self.send_response(200)
        self.send_header("Content-Type", "application/zip")
        self.send_header("Content-Disposition", f'attachment; filename="{name}.zip"')
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        try:
            with zipfile.ZipFile(_Stream(self.wfile), "w", zipfile.ZIP_DEFLATED) as z:
                for root, _, files in os.walk(local):
                    for f in files:
                        p = os.path.join(root, f)
                        z.write(p, os.path.relpath(p, local))
        except (BrokenPipeError, ConnectionResetError):
            pass  # download cancelled

    def log_message(self, fmt, *args):
        pass


def main():
    directory, port = sys.argv[1], int(sys.argv[2])
    handler = functools.partial(Handler, directory=directory)
    Handler.extensions_map.update({".fst": "application/octet-stream", ".oas": "application/octet-stream",
                                   ".gds": "application/octet-stream", ".log": "text/plain; charset=utf-8",
                                   ".v": "text/plain; charset=utf-8", ".md": "text/plain; charset=utf-8"})
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as httpd:
        httpd.serve_forever()


if __name__ == "__main__":
    main()
