"""Summarize board PERF logs and completed FFmpeg progress files.

Usage: python tools/analyze_usb_perf.py reports
No host/board clock subtraction: their intervals are independent.
"""
from pathlib import Path
import json
import re
import sys


def fields(line):
    return {k: float(v) if "." in v else int(v)
            for k, v in re.findall(r"(\w+)=([0-9.]+)", line)}


def board(path):
    blocks = []
    stats = []
    for line in path.read_text().splitlines():
        if line.startswith("stats "):
            stats.append(fields(line))
        if line.startswith("PERF total "):
            blocks.append({"total": fields(line), "metrics": {}})
        elif line.startswith("PERF metric=") and blocks:
            name = re.search(r"metric=(\w+)", line)[1]
            blocks[-1]["metrics"][name] = fields(line)
    assert blocks, f"No PERF records: {path}"
    # Earliest complete block at maximum submission count minimizes idle
    # capture samples after STREAMOFF in the original on-board sampler.
    maximum = max(b["total"]["sent"] for b in blocks)
    result = next(b for b in blocks if b["total"]["sent"] == maximum
                  and len(b["metrics"]) == 7)
    active = [s for s in stats if 0 < s["sent"] < maximum]
    if len(active) > 1:
        first, last = active[0], active[-1]
        seconds = (last["ns"] - first["ns"]) / 1e9
        result["steady_window"] = {"seconds": seconds}
        for name in ["capture", "sent", "returned", "invalid", "usb_buf_error",
                     "usb_owner_error", "usb_dq_error", "usb_event_error",
                     "usb_send_error", "dp_flips"]:
            delta = last[name] - first[name]
            result["steady_window"][name + "_delta"] = delta
            if name in ["capture", "sent", "returned", "dp_flips"]:
                result["steady_window"][name + "_fps"] = delta / seconds
    return result


def host(path):
    result = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            result[k] = v
    assert result.get("progress") == "end", f"Incomplete host run: {path}"
    return {"frames": int(result["frame"]), "fps_reported": float(result["fps"]),
            "media_seconds": int(result["out_time_us"]) / 1e6,
            "note": "fps_reported uses FFmpeg elapsed time; media_seconds uses stream timestamps"}


if __name__ == "__main__":
    root = Path(sys.argv[1])
    result = {}
    for tag, stem in [("A_live", "A90"), ("B_frozen", "B90"), ("A2_live", "A2_90")]:
        letter = tag.split("_")[0]
        result[tag] = board(root / f"perf_{letter}_board.log")
        result[tag]["host"] = host(root / f"perf_{stem}_host.progress")
    output = root / "usb_copy_performance_summary.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
