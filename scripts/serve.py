#!/usr/bin/env python3
"""Serve a build locally with the isolation headers needed by WASM threads."""

import argparse
import hashlib
import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit


class BuildRequestHandler(SimpleHTTPRequestHandler):
    evidence_limits = {
        "retail-port.jsonl": 256 * 1024 * 1024,
        "retail-timer.jsonl": 8 * 1024 * 1024,
        # Optional bounded hitch events include full nested phase/context data.
        "retail-browser-report.json": 8 * 1024 * 1024,
        "render-cache.db": 16 * 1024 * 1024,
        "render-cache.db-wal": 16 * 1024 * 1024,
    }

    def do_POST(self):
        """Explicit, same-origin local replay/cache diagnostic endpoint."""
        directory = self.server.evidence_directory
        name = self.path.removeprefix("/__melee_evidence/")
        limit = self.evidence_limits.get(name)
        if directory is None or self.path != f"/__melee_evidence/{name}" or limit is None:
            self.send_error(404)
            return
        host = f"127.0.0.1:{self.server.server_port}"
        if self.headers.get("Host") != host or self.headers.get("Origin") != f"http://{host}":
            self.send_error(403, "Evidence requires the loopback page origin")
            return
        if self.headers.get("Content-Type") != "application/octet-stream" or self.headers.get("Transfer-Encoding"):
            self.send_error(415)
            return
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self.send_error(411)
            return
        if not 0 < length <= limit:
            self.send_error(413)
            return
        self.connection.settimeout(30)
        try:
            data = self.rfile.read(length)
            if len(data) != length:
                self.send_error(400, "Incomplete evidence")
                return
            digest = hashlib.sha256(data).hexdigest()
            source = Path(name)
            output = directory / f"{source.stem}-{digest}{source.suffix}"
            try:
                with output.open("xb") as stream:
                    stream.write(data)
            except FileExistsError:
                if output.read_bytes() != data:
                    raise OSError("Existing evidence differs from its digest")
        except OSError:
            self.send_error(500, "Evidence could not be saved")
            return
        body = json.dumps({"path": str(output), "sha256": digest, "bytes": length}).encode()
        self.send_response(201)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

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


def create_server(directory, port=8787, evidence_directory=None):
    """Return a loopback server. Port zero is supported for test allocation."""
    if type(port) is not int or not 0 <= port <= 65535:
        raise ValueError("Port must be an integer between 0 and 65535")
    root = Path(directory).expanduser().resolve(strict=True)
    if not root.is_dir():
        raise NotADirectoryError(f"Build directory is not a directory: {root}")
    evidence = None
    if evidence_directory is not None:
        evidence = Path(evidence_directory).expanduser().resolve()
        if evidence.is_relative_to(root):
            raise ValueError("Evidence directory must be outside the served build")
        evidence.mkdir(parents=True, exist_ok=True)
    handler = partial(BuildRequestHandler, directory=str(root))
    server = ThreadingHTTPServer(("127.0.0.1", port), handler)
    server.evidence_directory = evidence
    return server


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
    parser.add_argument("--evidence-directory", help="Opt in to saving replay diagnostics outside the build directory")
    args = parser.parse_args()
    try:
        server = create_server(args.directory, args.port, args.evidence_directory)
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
