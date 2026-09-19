#!/usr/bin/env python3
"""Mock ESP32 for designing the dashboard with no hardware attached.

Serves index.html plus the exact JSON shape the firmware will serve, so the UI
never learns whether it is talking to a real box.

    python3 ui/serve.py            # then open http://localhost:8000
    python3 ui/serve.py --selftest # check the mock still matches the contract

Force a state while designing by opening http://localhost:8000/?s=<scenario>
    ok  warn  alert  fault  nobaseline  boot
"""

import argparse
import json
import math
import random
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

HISTORY_POINTS = 1080  # 3 h at one record per 10 s
INTERVAL_S = 10
START = time.time()
MUTED_UNTIL = 0.0

BANDS = {
    "temperature_c": (18, 30),
    "humidity_pct": (30, 70),
    "pressure_hpa": None,
    "air_index": (0, 1.5),
    "noise_db": (0, 75),
}


def _drift(base, swing, period_s, noise, t):
    return base + swing * math.sin(t / period_s) + random.uniform(-noise, noise)


def live_values(scenario, t):
    v = {
        "temperature_c": round(_drift(24.5, 1.8, 900, 0.15, t), 1),
        "humidity_pct": round(_drift(47, 7, 1300, 0.6, t), 1),
        "pressure_hpa": round(_drift(1008, 1.5, 4000, 0.2, t), 1),
        "air_index": round(max(0.6, _drift(1.05, 0.18, 700, 0.04, t)), 2),
        "noise_db": round(max(32, _drift(58, 9, 240, 2.5, t)), 1),
    }
    if scenario == "warn":
        v["humidity_pct"], v["noise_db"] = 78.4, 81.2
    elif scenario == "alert":
        v["air_index"], v["noise_db"] = 2.91, 72.0
    elif scenario == "fault":
        v["temperature_c"] = v["humidity_pct"] = v["pressure_hpa"] = None
    elif scenario == "nobaseline":
        v["air_index"] = None
    elif scenario == "boot":
        v = {k: None for k in v}
    return v


ALERT_ABOVE = {"air_index": 2.5}  # only gas escalates past "warn"


def level_for(key, value):
    """Severity of one reading: ok | warn | alert | offline."""
    if value is None:
        return "offline"
    band = BANDS[key]
    if band is None:
        return "ok"
    if key in ALERT_ABOVE and value > ALERT_ABOVE[key]:
        return "alert"
    return "ok" if band[0] <= value <= band[1] else "warn"


MESSAGES = {
    "ok": "Room is normal",
    "warn": "Humid and noisy — open a window",
    "alert": "Gas rising sharply — ventilate the room now",
    "fault": "BME280 is not responding — check the I²C wiring",
    "boot": "Starting up — warming the gas sensor",
    "nobaseline": "Running, but the air index needs a clean-air baseline",
}


def build_state(scenario):
    t = time.time() - START
    values = live_values(scenario, t)
    status = {"warn": "warn", "alert": "alert", "fault": "fault", "boot": "boot"}.get(scenario, "ok")

    readings = {}
    for key, value in values.items():
        entry = {"value": value, "level": level_for(key, value), "band": BANDS[key]}
        if value is None and scenario == "fault" and key != "air_index":
            entry["note"] = "no response"
        if key == "air_index" and scenario == "nobaseline":
            entry["note"] = "baseline not set"
        readings[key] = entry

    return {
        "uptime_s": int(t),
        "status": status,
        "message": MESSAGES.get(scenario, MESSAGES["ok"]),
        "muted_until_s": max(0, int(MUTED_UNTIL - time.time())),
        "gas_baseline_set": scenario != "nobaseline",
        "burn_in_complete": scenario not in ("boot", "nobaseline"),
        "readings": readings,
    }


def build_history(scenario):
    series = {k: [] for k in BANDS}
    for i in range(HISTORY_POINTS):
        t = (i - HISTORY_POINTS) * INTERVAL_S
        for key, value in live_values("ok", t).items():
            series[key].append(value)
    if scenario == "alert":  # show the ramp that led to the alert
        tail = 90
        for i in range(tail):
            series["air_index"][-tail + i] = round(1.1 + 1.8 * (i / tail) ** 2, 2)
    if scenario == "fault":  # the line should break, not drop to zero
        for key in ("temperature_c", "humidity_pct", "pressure_hpa"):
            series[key][-40:] = [None] * 40
    return {"interval_s": INTERVAL_S, "count": HISTORY_POINTS, "series": series}


class Handler(BaseHTTPRequestHandler):
    def _send(self, payload, code=200):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _scenario(self):
        _, _, query = self.path.partition("?")
        for part in query.split("&"):
            if part.startswith("s="):
                value = part[2:]
                return value if value in MESSAGES else "ok"
        return "ok"

    def do_GET(self):
        route = self.path.split("?")[0]
        if route == "/api/state":
            return self._send(build_state(self._scenario()))
        if route == "/api/history":
            return self._send(build_history(self._scenario()))
        if route in ("/", "/index.html"):
            page = (__file__.rsplit("/", 1)[0] or ".") + "/index.html"
            with open(page, "rb") as fh:
                body = fh.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            return self.wfile.write(body)
        self.send_error(404)

    def do_POST(self):
        global MUTED_UNTIL
        route = self.path.split("?")[0]
        if route == "/api/mute":
            MUTED_UNTIL = time.time() + 600
            return self._send({"muted_until_s": 600})
        if route == "/api/calibrate":
            return self._send({"calibrating": True, "seconds": 60})
        self.send_error(404)

    def log_message(self, *args):
        pass


def selftest():
    """Guards the UI/firmware JSON contract. Runs without a browser."""
    required = {"uptime_s", "status", "message", "muted_until_s",
                "gas_baseline_set", "burn_in_complete", "readings"}
    for scenario in MESSAGES:
        state = build_state(scenario)
        assert required <= set(state), f"{scenario}: missing {required - set(state)}"
        assert state["status"] in ("ok", "warn", "alert", "fault", "boot"), scenario
        assert set(state["readings"]) == set(BANDS), scenario
        for key, entry in state["readings"].items():
            assert set(entry) >= {"value", "level", "band"}, f"{scenario}/{key}"
            assert entry["level"] in ("ok", "warn", "alert", "offline"), f"{scenario}/{key}"
            # A missing reading must be null, never a fabricated zero.
            assert entry["value"] is None or isinstance(entry["value"], (int, float))
            assert (entry["value"] is None) == (entry["level"] == "offline"), f"{scenario}/{key}"

        history = build_history(scenario)
        assert set(history["series"]) == set(BANDS), scenario
        for key, points in history["series"].items():
            assert len(points) == HISTORY_POINTS, f"{scenario}/{key}"

    assert build_state("fault")["readings"]["temperature_c"]["value"] is None
    assert build_state("fault")["readings"]["temperature_c"]["level"] == "offline"
    assert build_state("alert")["readings"]["air_index"]["level"] == "alert"
    assert build_state("warn")["readings"]["humidity_pct"]["level"] == "warn"
    assert build_state("nobaseline")["gas_baseline_set"] is False
    assert None in build_history("fault")["series"]["temperature_c"]
    print("selftest ok")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    if args.selftest:
        selftest()
    else:
        print(f"mock classroom node on http://localhost:{args.port}")
        print("scenarios: ?s=warn  ?s=alert  ?s=fault  ?s=nobaseline  ?s=boot")
        HTTPServer(("", args.port), Handler).serve_forever()
