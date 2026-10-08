#!/usr/bin/env python3
"""Check source registration against the reviewed persistent compatibility table."""
import csv
import json
import math
import re
from pathlib import Path

module = Path(__file__).resolve().parents[1]
header = (module / "Parameters.hpp").read_text()
golden = json.loads((module / "parameter-golden.json").read_text())
assert golden["reviewStatus"] == "approved-numeric-mapping"
ids = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", header.split("};", 1)[0])}
source = []
for line in header.splitlines():
    if not line.startswith(' {"limiter.'):
        continue
    parts = next(csv.reader([line.strip().lstrip("{").rstrip(",").rstrip("}")], skipinitialspace=True))
    key, symbol, title, unit, lower, upper, initial, mapping, steps, auto, modes, transition, smoothing, *tail = parts
    lower, upper, initial = map(float, (lower, upper, initial))
    mapping = mapping.split("::")[1]
    row = {
        "stableKey": key, "paramID": ids[symbol], "title": title, "unit": unit,
        "min": lower, "max": upper, "default": initial, "mapping": mapping,
        "defaultNormalized": math.log(initial / lower) / math.log(upper / lower) if mapping == "logarithmic" else (initial - lower) / (upper - lower),
        "stepCount": int(steps), "automatable": auto == "true", "applicableModes": modes,
        "transition": transition.split("::")[1], "smoothingMs": float(smoothing),
    }
    if symbol in ("mode", "algorithmVersion"):
        name = "modeNames" if symbol == "mode" else "algorithmNames"
        match = re.search(name + r"\[\]=\{([^}]+)\}", header)
        labels = next(csv.reader([match.group(1)]))
        row["enumOrdinals"] = {label: index for index, label in enumerate(labels)}
    if symbol == "algorithmVersion":
        row["restoreMissingDefault"] = float(tail[1])
    source.append(row)
assert source == golden["parameters"], "Compatibility table changed: review migration before replacing the golden"
assert source == json.loads((module / "manifest.json").read_text())["parameterSpecs"]
print("PASS approved Limiter eleven-row IDs, ranges, units, enums, normalized Init, transitions, and smoothing")
