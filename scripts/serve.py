#!/usr/bin/env python3
"""Serve a build locally with the isolation headers needed by WASM threads."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit


class BuildRequestHandler(SimpleHTTPRequestHandler):
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def _inside_root(self, path):
        root = Path(self.directory)
        resolved = path.resolve()
        if not resolved.is_relative_to(root):
            raise PermissionError("Path leaves the build directory")
        return resolved

    def translate_path(self, path):
        url = urlsplit(path)
        decoded = unquote(url.path, errors="strict")
        if url.scheme or url.netloc or ".." in decoded.split("/") or "\\" in decoded:
            raise PermissionError("Invalid build path")
        if "\x00" in decoded:
            raise ValueError("Invalid build path")
        return str(self._inside_root(Path(self.directory) / decoded.lstrip("/")))

    def send_head(self):
        try:
            path = Path(self.translate_path(self.path))
            # SimpleHTTPRequestHandler follows index files after resolving a directory.
            # Check these as well: an index can itself be a symlink outside the root.
            if path.is_dir():
                for name in ("index.html", "index.htm"):
                    index = path / name
                    if index.exists():
                        self._inside_root(index)
                        break
            return super().send_head()
        except PermissionError:
            self.send_error(403, "Path is outside the served build directory")
        except (ValueError, UnicodeError, RuntimeError):
            self.send_error(400, "Invalid request path")
        return None


def create_server(directory, port=8787):
    """Return a loopback server. Port zero is supported for test allocation."""
    if type(port) is not int or not 0 <= port <= 65535:
        raise ValueError("Port must be an integer between 0 and 65535")
    root = Path(directory).expanduser().resolve(strict=True)
    if not root.is_dir():
        raise NotADirectoryError(f"Build directory is not a directory: {root}")
    handler = partial(BuildRequestHandler, directory=str(root))
    return ThreadingHTTPServer(("127.0.0.1", port), handler)


def cli_port(value):
    try:
        port = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("Port must be an integer") from error
    if not 1 <= port <= 65535:
        raise argparse.ArgumentTypeError("Port must be between 1 and 65535")
    return port


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True, help="Existing build directory to serve")
    parser.add_argument("--port", type=cli_port, default=8787)
    args = parser.parse_args()
    try:
        server = create_server(args.directory, args.port)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    with server:
        print(f"Serving {Path(args.directory).resolve()} at http://127.0.0.1:{args.port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
