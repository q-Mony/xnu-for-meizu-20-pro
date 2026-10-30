#!/usr/bin/env python3
"""Offline measurements of retained boot logs; never contacts the device.

Default inputs are four distinct historical logs. No claim that the samples
reproduce the live bug or capture Boot and first-Hello presentation timestamps.
Output is stdout; redirect to a NEW file to preserve earlier evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


NOVA = Path(__file__).resolve().parents[2]
SNAPSHOT = NOVA.parent / "snapshot"
DEFAULTS = [
    "autolog-20261029-032506.txt",
    "autolog-20261028-151829.txt",
    "autolog-20261028-214724.txt",
    "autolog-20261029-022743.txt",
]


def fields(line):
    return dict(re.findall(r"\b([A-Za-z_]+)=([^\s]+)", line))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def analyze(path):
    raw = path.read_bytes()
    lines = raw.decode(errors="replace").splitlines()
    names, receivers, waits = {}, {}, {}
    pairs, active_sig, last_open = [], {}, None
    ticks, events, markers, watchdogs, attempts = [], [], {}, [], []
    for line_no, line in enumerate(lines, 1):
        f = fields(line)
        if "meizu: exec-map " in line and {"pid", "taskgen", "name"} <= f.keys():
            names[(f["pid"], f["taskgen"])] = f["name"]
        if "meizu: receiver-map " in line and f.get("status") == "matched":
            receivers[f["seq"]] = (f["pid"], f["taskgen"], f["name"])
        if "meizu: sbipc latest " in line and f.get("phase") == "4" and f.get("pending") == "1":
            key = (f["pid"], f["tid"], f["seq"])
            w = waits.setdefault(key, {"pid": int(f["pid"]), "tid": int(f["tid"]),
                "seq": int(f["seq"]), "first_line": line_no, "last_line": line_no,
                "samples": [], "max_wait_age_ms": 0})
            w["samples"].append({"line": line_no, "age_ms": int(f["age_ms"])})
            w["last_line"] = line_no
            if int(f["age_ms"]) >= w["max_wait_age_ms"]:
                w.update(max_wait_age_ms=int(f["age_ms"]), max_age_line=line_no,
                    endpoint_pid=int(f.get("end_pid", -1)),
                    endpoint_generation=int(f.get("taskgen", 0)))
        m = re.search(r"meizu: hb tick=(\d+)", line)
        if m:
            ticks.append({"line": line_no, "tick": int(m[1])})
        m = re.search(r"meizu: open '(dyld_shared_cache_arm64[^']*)'", line)
        if m:
            last_open = m[1]
            markers.setdefault("first_dyld_open_line", line_no)
        if "meizu: addfilesigs pid=" in line:
            active_sig[f["pid"]] = {"start_line": line_no, "pid": int(f["pid"]),
                "cmd": int(f["cmd"]), "signature_bytes": int(f["size"]),
                "signature_file_offset": int(f["off"]), "last_observed_open": last_open,
                "preceding_heartbeat_tick": ticks[-1]["tick"] if ticks else None}
        if "meizu: addfilesigs result=" in line and f["pid"] in active_sig:
            p = active_sig.pop(f["pid"])
            inside = [x["tick"] for x in ticks if p["start_line"] < x["line"] < line_no]
            p.update(end_line=line_no, result=int(f["result"]), heartbeat_samples=inside)
            p["intra_sample_tick_span"] = inside[-1] - inside[0] if len(inside) > 1 else 0
            pairs.append(p)
        if "meizu: srmas2 enter " in line:
            markers.setdefault("shared_region_map_enter_line", line_no)
        if "NEGRONI FB: swap " in line:
            markers.setdefault("first_framebuffer_swap_log_line", line_no)
        if "meizu: setup-arm " in line:
            attempts.append({"line": line_no, **{k: f[k] for k in ("pid", "taskgen", "tid", "attempt")}})
        m = re.search(r"\[System Event\] \[(\d+)\].*\[Event: ([^]]+)\] (.*)", line)
        if m:
            events.append({"line": line_no, "raw_continuous_ticks": int(m[1]), "event": m[2], "message": m[3]})
        m = re.search(r"comm=Setup .*scene-create watchdog.*allowance of ([\d.]+) seconds", line)
        if m:
            watchdogs.append({"line": line_no, "pid": int(f["pid"]), "wall_allowance_seconds": float(m[1])})
    for w in waits.values():
        endpoint = (str(w["endpoint_pid"]), str(w["endpoint_generation"]))
        w["endpoint_name"] = names.get(endpoint, "unknown")
        r = receivers.get(str(w["seq"]))
        if r and r[:2] == endpoint:
            w["endpoint_name"] = r[2]
    waits = sorted(waits.values(), key=lambda w: w["first_line"])
    long_waits = [w for w in waits if w["max_wait_age_ms"] >= 10000]
    # Summing is only valid for distinct waits on the SAME SB thread, in order.
    # Report individual maxima; never sum arbitrary service/worker waits.
    main_pair = []
    for a in long_waits:
        for b in long_waits:
            if (a["endpoint_name"] == "launchd" and b["endpoint_name"] == "lsd"
                    and (a["pid"], a["tid"]) == (b["pid"], b["tid"])
                    and a["last_line"] < b["first_line"]):
                main_pair.append({"launchd_seq": a["seq"], "lsd_seq": b["seq"],
                    "pid": a["pid"], "tid": a["tid"],
                    "serial_wait_lower_bound_ms": a["max_wait_age_ms"] + b["max_wait_age_ms"]})
    cutoff = markers.get("shared_region_map_enter_line", 0)
    before_map = [x for x in ticks if x["line"] < cutoff]
    return {"path": str(path.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
        "bytes": len(raw), "markers": markers, "long_pending_sb_waits": long_waits,
        "serial_launchd_lsd_pairs": main_pair, "observed_signature_pairs": pairs,
        "heartbeat_before_shared_map": before_map[-1] if before_map else None,
        "system_events": events, "setup_arms": attempts, "setup_watchdogs": watchdogs}


def contextual_frames():
    """Reuse exact unslid PC matches in a prior symbol report; no new symbols.

    This is a contextual match to the same OS cache family, not an independent
    UUID-certified re-symbolication. Do not interpret distant nearest stub
    symbols as actual calls. Unmatched frames stay unknown.
    """
    previous = SNAPSHOT / "lsdbt-144210-symbols.json"
    known = {}
    for trace in json.loads(previous.read_text()):
        for f in trace["frames"]:
            if f.get("nearest_symbol"):
                known[int(f["unslid"], 16)] = (f["nearest_symbol"], f["offset"])
    result = []
    log = SNAPSHOT / DEFAULTS[0]
    for n, line in enumerate(log.read_text().splitlines(), 1):
        m = re.search(r"lsdbt-pc seq=(\d+) frame=(\d+) pc=(0x[0-9a-f]+)", line)
        if not m:
            continue
        seq, frame, pc = m.groups()
        va = int(pc, 16) - 0x43E38000  # log: shared region start=0x1c3e38000
        symbol = known.get(va)
        if symbol and any(x in symbol[0] for x in ("LSDatabase", "LSPreferences", "LSHandler", "SEEDING", "scanAppsDirectory", "container_")):
            result.append({"line": n, "seq": int(seq), "frame": int(frame),
                "pc": pc, "unslid_pc": hex(va), "prior_nearest_symbol": symbol[0], "offset": symbol[1]})
    return {"prior_symbol_report": str(previous.resolve()), "sha256": digest(previous),
        "qualification": "Exact unslid-PC contextual matches; no fresh cache UUID validation", "frames": result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", type=Path)
    args = parser.parse_args()
    logs = args.logs or [SNAPSHOT / n for n in DEFAULTS]
    output = {"scope": "Offline evidence measurement; no device reproduction or functional change",
        "limits": ["No timestamped Boot action or first Hello frame is captured",
            "Pending phase4 age_ms is a measured wait age, not boot uptime",
            "Heartbeat tick counts dump calls; tick*0.05 is nominal only",
            "Watchdog allowances must not be blindly added to time-to-first-Hello",
            "Log filenames are extraction times, not boot/Hello timestamps"],
        "logs": [analyze(p) for p in logs]}
    if not args.logs:
        output["lsd_contextual_symbols_032506"] = contextual_frames()
        primary = output["logs"][0]
        w = {x["seq"]: x for x in primary["long_pending_sb_waits"]}
        assert w[261]["max_wait_age_ms"] == 35296 and w[261]["endpoint_name"] == "launchd"
        assert w[2376]["max_wait_age_ms"] == 100830 and w[2376]["endpoint_name"] == "lsd"
        assert len({r["sha256"] for r in output["logs"]}) == len(logs), "Duplicate logs are not independent boots"
        # Only this frozen artifact's actual ADT was verified at 19.2 MHz.
        e = primary["system_events"]
        first = next(x for x in e if x["event"] == "fsck")
        last = next(x for x in e if x["event"] == "boot" and "Early boot complete" in x["message"])
        primary["launchd_boot_task_event_delta_seconds"] = (last["raw_continuous_ticks"] - first["raw_continuous_ticks"]) / 19200000
        primary["clock_frequency_hz"] = 19200000
    print(json.dumps(output, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
