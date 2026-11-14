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
POST /_mock/reset. Faults apply to the reply of a request after its effect,
so a dropped start reply still started the print, as a lost reply would.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlsplit

DEFAULT_STATE = {
    "klippy_state": "ready",           # /server/info and webhooks.state
    "hostname": "seeker3-mock",        # /printer/info hostname
    "hostname_after_upload": None,     # if set, /printer/info reports this once a file is uploaded
    "print_state": "standby",          # print_stats.state
    "print_filename": "",              # print_stats.filename
    "sd_file_path": None,              # virtual_sdcard.file_path
    "sdcard_active": False,            # virtual_sdcard.is_active
    "omit": [],                        # "object.field" names left out of status replies
    "override": {},                    # "object.field": value, replacing status values (e.g. malformed types)
    "nozzles": {"extruder": 0.7, "extruder1": 0.4},  # T0 composite, T1 polymer
    "start_status": 200,               # HTTP status of POST /printer/print/start
    "state_after_start": "printing",   # print_stats.state once a print is started
    "corrupt_stored_copy": False,      # serve different bytes than were uploaded
    "start_on_upload": False,          # claim print_started in the upload reply
    "rename_upload": None,             # store the upload under this name and report it
    "delay": {},                       # path: seconds to wait before replying
    "drop": [],                        # paths whose connection is closed without a reply
    "redirect": {},                    # path: URL to answer with 307 instead (no effect)
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
            objects[name] = {"is_active": STATE["sdcard_active"], "file_path": STATE["sd_file_path"]}
        elif name == "configfile":
            config = {section: {"nozzle_diameter": value if isinstance(value, str) else f"{value:.3f}"}
                      for section, value in STATE["nozzles"].items()}
            config["heater_bed"] = {"max_temp": "120"}
            objects[name] = {"config": config} if attrs in ("", "config") else {}
    for key in STATE["omit"]:
        obj, _, field = key.partition(".")
        objects.get(obj, {}).pop(field, None)
    for key, value in STATE["override"].items():
        obj, _, field = key.partition(".")
        if obj in objects:
            objects[obj][field] = value
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


def error(status: int, message: str):
    return status, {"error": {"code": status, "message": message}}, None


def route(method: str, path: str, query: str, headers, data: bytes):
    """Apply a request's effect and return (status, json payload, raw bytes)."""
    if method == "GET":
        if path == "/server/info":
            return 200, {"result": {"klippy_connected": True, "klippy_state": STATE["klippy_state"]}}, None
        if path == "/printer/info":
            hostname = STATE["hostname"]
            if FILES and STATE["hostname_after_upload"]:
                hostname = STATE["hostname_after_upload"]
            return 200, {"result": {"state": STATE["klippy_state"], "hostname": hostname}}, None
        if path == "/printer/objects/query":
            return 200, {"result": {"eventtime": 1.0, "status": status_objects(query)}}, None
        if path == "/server/files/metadata":
            name = parse_qs(query).get("filename", [""])[0]
            if name in FILES:
                return 200, {"result": {"filename": name, "size": len(FILES[name])}}, None
            return error(404, f"Metadata not available for <{name}>")
        if path.startswith("/server/files/gcodes/"):
            name = path[len("/server/files/gcodes/"):]
            if name not in FILES:
                return error(404, f"File {name} does not exist")
            stored = FILES[name]
            return 200, None, stored[:-1] + b"X" if STATE["corrupt_stored_copy"] else stored
        return error(404, "Not found")
    if path == "/server/files/upload":
        fields = parse_multipart(data, headers.get("Content-Type", ""))
        filename, payload = fields.get("file", (None, b""))
        root = fields.get("root", (None, b"gcodes"))[1].decode()
        checksum = fields.get("checksum", (None, b""))[1].decode()
        LOG[-1]["fields"] = {k: v[1].decode() for k, v in fields.items() if k != "file"}
        LOG[-1]["filename"] = filename
        if not filename or root != "gcodes":
            return error(400, "Bad upload")
        if checksum and checksum != hashlib.sha256(payload).hexdigest():
            return error(422, "File checksum mismatch")
        stored_as = STATE["rename_upload"] or filename
        FILES[stored_as] = payload
        return 201, {"item": {"path": stored_as, "root": "gcodes", "size": len(payload), "modified": 1.0},
                     "print_started": STATE["start_on_upload"], "print_queued": False,
                     "action": "create_file"}, None
    if path == "/printer/print/start":
        name = json.loads(data or b"{}").get("filename", "") or parse_qs(query).get("filename", [""])[0]
        STARTS.append(name)
        if STATE["start_status"] != 200:
            return error(STATE["start_status"], "Start refused by mock")
        if name not in FILES:
            return error(400, f"File {name} does not exist")
        STATE["print_state"] = STATE["state_after_start"]
        STATE["print_filename"] = name
        return 200, {"result": "ok"}, None
    return error(404, "Not found")


class Handler(BaseHTTPRequestHandler):
    server_version = "MockFibreSeek/2"
    protocol_version = "HTTP/1.1"  # every reply sets Content-Length

    def log_message(self, *args) -> None:  # keep test output quiet
        pass

    def send(self, status: int, payload, raw: bytes | None = None, location: str | None = None) -> None:
        body = raw if raw is not None else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream" if raw is not None else "application/json")
        self.send_header("Content-Length", str(len(body)))
        if location:
            self.send_header("Location", location)
        self.end_headers()
        self.wfile.write(body)

    def handle_request(self, method: str) -> None:
        url = urlsplit(self.path)
        path = unquote(url.path)
        data = self.rfile.read(int(self.headers.get("Content-Length", "0") or 0)) if method == "POST" else b""
        with LOCK:
            if path == "/_mock/log":
                return self.send(200, {"requests": LOG, "starts": STARTS, "files": sorted(FILES), "state": STATE})
            if path == "/_mock/reset":
                reset()
                return self.send(200, {"result": "ok"})
            if path == "/_mock/state":
                STATE.update(json.loads(data or b"{}"))
                if "files" in STATE:  # preload files: {"files": {"name.gcode": "text"}}
                    FILES.update({k: v.encode() for k, v in STATE.pop("files").items()})
                return self.send(200, {"result": "ok"})
            LOG.append({"method": method, "path": path, "query": url.query})
            if path in STATE["redirect"]:
                return self.send(307, {"result": "moved"}, location=STATE["redirect"][path])
            status, payload, raw = route(method, path, url.query, self.headers, data)
            delay = STATE["delay"].get(path, 0)
            drop = path in STATE["drop"]
        if delay:
            time.sleep(delay)
        if drop:
            self.close_connection = True  # no reply at all
            return
        self.send(status, payload, raw)

    def do_GET(self) -> None:
        self.handle_request("GET")

    def do_POST(self) -> None:
        self.handle_request("POST")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=7125, help="port on 127.0.0.1 (0 picks a free one)")
    parser.add_argument("--port-file", help="write the chosen port to this file once listening")
    args = parser.parse_args()
    reset()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
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
