#!/usr/bin/env python3
"""Minimal static file server with COOP/COEP headers.

Required to serve a threaded (pthreads) emscripten build: browsers only enable
SharedArrayBuffer when the document is cross-origin isolated, which needs
Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp.

Usage: python3 wasm_server.py [directory] [port]
"""
import http.server
import socketserver
import sys
import os

DIRECTORY = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "out/wasm/delivery")
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


if __name__ == "__main__":
    with ReusableTCPServer(("", PORT), Handler) as httpd:
        print(f"Serving {DIRECTORY} on http://localhost:{PORT} (COOP/COEP enabled)")
        httpd.serve_forever()
