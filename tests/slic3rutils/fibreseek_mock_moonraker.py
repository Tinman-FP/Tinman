#!/usr/bin/env python3
"""Mock FibreSeek Seeker 3 for testing Tinman's FibreSeek print host.

It answers the Moonraker web API calls that src/slic3r/Utils/FibreSeek.cpp
makes, keeps uploaded files in memory and records every request. It never
contacts a printer and only listens on 127.0.0.1.

Used by tests/slic3rutils/test_fibreseek_send.cpp, and by hand for a GUI
dry run:

    python3 tests/slic3rutils/fibreseek_mock_moonraker.py --port 7125

then set a physical printer's host type to "FibreSeek Seeker 3" and its
address to 127.0.0.1:7125, and send a job from Tinman.

Tests change the mock's state with POST /_mock/state (a JSON object merged
into STATE below), read it back with GET /_mock/log, and clear it with
POST /_mock/reset.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlsplit

DEFAULT_STATE = {
    "klippy_state": "ready",          # /server/info and webhooks.state
    "print_state": "standby",         # print_stats.state
    "print_filename": "",             # print_stats.filename
    "sdcard_active": False,           # virtual_sdcard.is_active
    "nozzles": [0.4, 0.7],            # nozzle_diameter of [extruder], [extruder1], ...
    "start_status": 200,              # HTTP status of POST /printer/print/start
    "state_after_start": "printing",  # print_stats.state once a print is started
    "corrupt_stored_copy": False,     # serve different bytes than were uploaded
    "start_on_upload": False,         # claim print_started in the upload reply
}

LOCK = threading.Lock()
STATE: dict = {}
FILES: dict[str, bytes] = {}
LOG: list[dict] = []
STARTS: list[str] = []


def reset() -> None:
    STATE.clear()
    STATE.update(copy.deepcopy(DEFAULT_STATE))
    FILES.clear()
    LOG.clear()
    STARTS.clear()


def status_objects(query: str) -> dict:
    """Build the status of the objects named in a /printer/objects/query string."""
    objects = {}
    for part in query.split("&"):
        if not part:
            continue
        name, _, attrs = part.partition("=")
        name = unquote(name)
        if name == "webhooks":
            objects[name] = {"state": STATE["klippy_state"], "state_message": "mock"}
        elif name == "print_stats":
            objects[name] = {"state": STATE["print_state"], "filename": STATE["print_filename"], "message": ""}
        elif name == "virtual_sdcard":
            objects[name] = {"is_active": STATE["sdcard_active"], "file_path": None}
        elif name == "configfile":
            config = {}
            for index, diameter in enumerate(STATE["nozzles"]):
                config["extruder" + (str(index) if index else "")] = {"nozzle_diameter": f"{diameter:.3f}"}
            config["heater_bed"] = {"max_temp": "120"}
            objects[name] = {"config": config} if attrs in ("", "config") else {}
    return objects


def parse_multipart(body: bytes, content_type: str) -> dict[str, tuple[str | None, bytes]]:
    """Minimal multipart/form-data parser: {field: (filename, value)}."""
    boundary = content_type.split("boundary=", 1)[1].strip('"').encode()
    fields = {}
    for part in body.split(b"--" + boundary):
        if b"\r\n\r\n" not in part:
            continue
        head, value = part.split(b"\r\n\r\n", 1)
        if value.endswith(b"\r\n"):
            value = value[:-2]
        disposition = next((line for line in head.decode("utf-8", "replace").split("\r\n")
                            if line.lower().startswith("content-disposition")), "")
        params = dict(item.strip().split("=", 1) for item in disposition.split(";")[1:] if "=" in item)
        name = params.get("name", "").strip('"')
        filename = params["filename"].strip('"') if "filename" in params else None
        fields[name] = (filename, value)
    return fields


class Handler(BaseHTTPRequestHandler):
    server_version = "MockFibreSeek/1"
    protocol_version = "HTTP/1.1"  # every reply sets Content-Length

    def log_message(self, *args) -> None:  # keep test output quiet
        pass

    def reply(self, status: int, payload, raw: bytes | None = None) -> None:
        body = raw if raw is not None else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream" if raw is not None else "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def error(self, status: int, message: str) -> None:
        self.reply(status, {"error": {"code": status, "message": message}})

    def body(self) -> bytes:
        return self.rfile.read(int(self.headers.get("Content-Length", "0") or 0))

    def do_GET(self) -> None:
        url = urlsplit(self.path)
        with LOCK:
            if not url.path.startswith("/_mock/"):
                LOG.append({"method": "GET", "path": unquote(url.path), "query": url.query})
            if url.path == "/_mock/log":
                self.reply(200, {"requests": LOG, "starts": STARTS, "files": sorted(FILES), "state": STATE})
            elif url.path == "/server/info":
                self.reply(200, {"result": {"klippy_connected": True, "klippy_state": STATE["klippy_state"]}})
            elif url.path == "/printer/objects/query":
                self.reply(200, {"result": {"eventtime": 1.0, "status": status_objects(url.query)}})
            elif url.path == "/server/files/metadata":
                name = parse_qs(url.query).get("filename", [""])[0]
                if name in FILES:
                    self.reply(200, {"result": {"filename": name, "size": len(FILES[name])}})
                else:
                    self.error(404, f"Metadata not available for <{name}>")
            elif url.path.startswith("/server/files/gcodes/"):
                name = unquote(url.path[len("/server/files/gcodes/"):])
                if name not in FILES:
                    self.error(404, f"File {name} does not exist")
                else:
                    data = FILES[name]
                    self.reply(200, None, data[:-1] + b"X" if STATE["corrupt_stored_copy"] else data)
            else:
                self.error(404, "Not found")

    def do_POST(self) -> None:
        url = urlsplit(self.path)
        data = self.body()
        with LOCK:
            if url.path == "/_mock/reset":
                reset()
                return self.reply(200, {"result": "ok"})
            if url.path == "/_mock/state":
                STATE.update(json.loads(data or b"{}"))
                if "files" in STATE:  # preload files: {"files": {"name.gcode": "text"}}
                    FILES.update({k: v.encode() for k, v in STATE.pop("files").items()})
                return self.reply(200, {"result": "ok"})
            LOG.append({"method": "POST", "path": unquote(url.path), "query": url.query})
            if url.path == "/server/files/upload":
                fields = parse_multipart(data, self.headers.get("Content-Type", ""))
                filename, payload = fields.get("file", (None, b""))
                root = fields.get("root", (None, b"gcodes"))[1].decode()
                checksum = fields.get("checksum", (None, b""))[1].decode()
                LOG[-1]["fields"] = {k: v[1].decode() for k, v in fields.items() if k != "file"}
                if not filename or root != "gcodes":
                    return self.error(400, "Bad upload")
                if checksum and checksum != hashlib.sha256(payload).hexdigest():
                    return self.error(422, "File checksum mismatch")
                FILES[filename] = payload
                self.reply(201, {"item": {"path": filename, "root": "gcodes", "size": len(payload), "modified": 1.0},
                                 "print_started": STATE["start_on_upload"], "print_queued": False,
                                 "action": "create_file"})
            elif url.path == "/printer/print/start":
                name = json.loads(data or b"{}").get("filename", "") or parse_qs(url.query).get("filename", [""])[0]
                STARTS.append(name)
                if STATE["start_status"] != 200:
                    return self.error(STATE["start_status"], "Start refused by mock")
                if name not in FILES:
                    return self.error(400, f"File {name} does not exist")
                STATE["print_state"] = STATE["state_after_start"]
                STATE["print_filename"] = name
                self.reply(200, {"result": "ok"})
            else:
                self.error(404, "Not found")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=7125, help="port on 127.0.0.1 (0 picks a free one)")
    parser.add_argument("--port-file", help="write the chosen port to this file once listening")
    args = parser.parse_args()
    reset()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    port = server.server_address[1]
    if args.port_file:
        with open(args.port_file, "w", encoding="utf-8") as output:
            output.write(str(port))
    print(f"Mock FibreSeek Seeker 3 (Moonraker API) on http://127.0.0.1:{port}; no printer is contacted.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
