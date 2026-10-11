#!/usr/bin/env python3
"""Serve the Halo web build with byte ranges and cross-origin isolation."""

from __future__ import annotations

import argparse
import datetime
import email.utils
import os
import shutil
import sys
import urllib.parse
import webbrowser
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import BinaryIO, Iterable


COPY_BUFFER_SIZE = 1 << 20


class HaloRequestHandler(SimpleHTTPRequestHandler):
    """Static files, including single HTTP byte ranges used by FetchFS."""

    protocol_version = "HTTP/1.1"

    def __init__(self, *args: object, directory: str, **kwargs: object) -> None:
        self._range_remaining: int | None = None
        super().__init__(*args, directory=directory, **kwargs)

    def end_headers(self) -> None:
        # SharedArrayBuffer (and therefore Emscripten pthreads) requires a
        # cross-origin-isolated page.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        # This is a local development server. Revalidate every artifact so
        # replacing an extracted ISO cannot leave Chrome using stale map data.
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def guess_type(self, path: str) -> str:
        lowered = path.lower()
        if lowered.endswith(".wasm"):
            return "application/wasm"
        if lowered.endswith((".js", ".mjs")):
            return "text/javascript; charset=utf-8"
        if "/assets/maps/" in lowered.replace(os.sep, "/"):
            return "application/octet-stream"
        return super().guess_type(path)

    @staticmethod
    def _parse_range(value: str, size: int) -> tuple[int, int]:
        if not value.startswith("bytes=") or "," in value:
            raise ValueError("only one bytes range is supported")
        bounds = value[6:].strip()
        if "-" not in bounds:
            raise ValueError("invalid byte range")
        first, last = bounds.split("-", 1)
        if not first:
            if not last.isdigit() or int(last) <= 0:
                raise ValueError("invalid suffix byte range")
            length = min(int(last), size)
            if not length:
                raise ValueError("range on an empty file")
            return size - length, size - 1
        if not first.isdigit() or (last and not last.isdigit()):
            raise ValueError("invalid byte range")
        start = int(first)
        end = int(last) if last else size - 1
        if start >= size or start > end:
            raise ValueError("unsatisfiable byte range")
        return start, min(end, size - 1)

    def _range_not_satisfiable(self, size: int) -> None:
        self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
        self.send_header("Content-Range", f"bytes */{size}")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def send_head(self) -> BinaryIO | None:
        self._range_remaining = None
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            parts = urllib.parse.urlsplit(self.path)
            if not parts.path.endswith(("/", "%2f", "%2F")):
                self.send_response(HTTPStatus.MOVED_PERMANENTLY)
                new_parts = (parts[0], parts[1], parts[2] + "/", parts[3], parts[4])
                self.send_header("Location", urllib.parse.urlunsplit(new_parts))
                self.send_header("Content-Length", "0")
                self.end_headers()
                return None
            for index_name in getattr(self, "index_pages", ("index.html", "index.htm")):
                candidate = os.path.join(path, index_name)
                if os.path.isfile(candidate):
                    path = candidate
                    break
            else:
                return self.list_directory(path)

        if path.endswith("/"):
            self.send_error(HTTPStatus.NOT_FOUND, "File not found")
            return None
        try:
            source = open(path, "rb")
        except OSError:
            self.send_error(HTTPStatus.NOT_FOUND, "File not found")
            return None

        try:
            file_stat = os.fstat(source.fileno())
            size = file_stat.st_size
            range_header = self.headers.get("Range")
            if range_header:
                try:
                    start, end = self._parse_range(range_header, size)
                except ValueError:
                    source.close()
                    self._range_not_satisfiable(size)
                    return None
                length = end - start + 1
                source.seek(start)
                self._range_remaining = length
                self.send_response(HTTPStatus.PARTIAL_CONTENT)
                self.send_header("Content-Type", self.guess_type(path))
                self.send_header("Accept-Ranges", "bytes")
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
                self.send_header("Content-Length", str(length))
                self.send_header("Last-Modified", self.date_time_string(file_stat.st_mtime))
                self.end_headers()
                return source

            if "If-Modified-Since" in self.headers and "If-None-Match" not in self.headers:
                try:
                    modified_since = email.utils.parsedate_to_datetime(
                        self.headers["If-Modified-Since"]
                    )
                except (TypeError, IndexError, OverflowError, ValueError):
                    modified_since = None
                if modified_since is not None:
                    if modified_since.tzinfo is None:
                        modified_since = modified_since.replace(tzinfo=datetime.timezone.utc)
                    if modified_since.tzinfo is datetime.timezone.utc:
                        last_modified = datetime.datetime.fromtimestamp(
                            file_stat.st_mtime, datetime.timezone.utc
                        ).replace(microsecond=0)
                        if last_modified <= modified_since:
                            self.send_response(HTTPStatus.NOT_MODIFIED)
                            self.end_headers()
                            source.close()
                            return None

            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", self.guess_type(path))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Length", str(size))
            self.send_header("Last-Modified", self.date_time_string(file_stat.st_mtime))
            self.end_headers()
            return source
        except Exception:
            source.close()
            raise

    def copyfile(self, source: BinaryIO, outputfile: BinaryIO) -> None:
        if self._range_remaining is None:
            shutil.copyfileobj(source, outputfile, COPY_BUFFER_SIZE)
            return
        remaining = self._range_remaining
        while remaining:
            chunk = source.read(min(COPY_BUFFER_SIZE, remaining))
            if not chunk:
                break
            outputfile.write(chunk)
            remaining -= len(chunk)


def main(argv: Iterable[str] | None = None) -> int:
    repository = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=repository, help="directory to serve")
    parser.add_argument("--bind", default="127.0.0.1", help="address to bind (default: %(default)s)")
    parser.add_argument("--port", type=int, default=8000, help="TCP port (default: %(default)s)")
    parser.add_argument("--open", action="store_true", help="open Halo in the default browser")
    arguments = parser.parse_args(argv)

    root = arguments.root.expanduser().resolve(strict=True)
    if not root.is_dir():
        parser.error(f"not a directory: {root}")

    def handler(*args: object, **kwargs: object) -> HaloRequestHandler:
        return HaloRequestHandler(*args, directory=str(root), **kwargs)

    server = ThreadingHTTPServer((arguments.bind, arguments.port), handler)
    host, port = server.server_address[:2]
    browser_host = "127.0.0.1" if host == "0.0.0.0" else str(host)
    url = f"http://{browser_host}:{port}/build/browser/halo.html"
    print(f"Serving {root}", flush=True)
    print(f"Halo URL: {url}", flush=True)
    if arguments.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
